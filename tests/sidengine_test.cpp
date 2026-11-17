#include "../sidengine.h"
#include "fixtures_lfpg.h"
#include <cstdlib>
#include <iostream>

using namespace vsid::v2;
using Json = nlohmann::ordered_json;

void check(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

AirportConfig parseOk(const Json& json)
{
	AirportConfig config;
	std::string error;
	const bool ok = parseAirport(json, "LFPG", config, error);
	if (!ok) std::cerr << "Unexpected parse error: " << error << '\n';
	check(ok, "config must parse");
	return config;
}

AirportConfig lfpg() { return parseOk(fixtures::lfpg()); }

Request request(const AirportConfig& cfg, const std::string& waypoint)
{
	Request req;
	req.waypoint = waypoint;
	req.depRwys = { "27L", "26R" };
	req.arrRwys = { "27R", "26L" };
	req.equipment = "SDFGRY";
	for (const auto& e : cfg.entries) req.available.insert(e.id);
	return req;
}

// designator of the selected entry, "" if none
std::string sidOf(const AirportConfig& cfg, const Result& r)
{
	if (!r.entryId) return "";
	return cfg.entries.at(*r.entryId).designator;
}

// small airport without runwayGroups: waypoint ABC with the given entries
AirportConfig plain(const std::string& entries, const std::string& extra = "")
{
	return parseOk(Json::parse(R"({ "version": 2, "runways": "27L,27R,26L,26R", "customRules": { "wlpg": false, "elpg": false },
		"defaults": { "equip": { "RNAV": true }, "initialClimb": { "J": 6000, "P": 4000, "T": 4000 } },
		"procedures": { "ABC": [)" + entries + "]}" + extra + "}"));
}

const GeoPoint inSouth{ 48.995, 2.535 };   // south only
const GeoPoint inNeutral{ 48.997, 2.575 }; // south and t2-mid (default)

void testNominalNorth()
{
	const auto cfg = lfpg();
	const auto r = select(cfg, request(cfg, "DIKOL"));
	check(sidOf(cfg, r) == "A" && r.rwy == "27L" && r.initialClimb == 10000 && !r.climbvia, "DIKOL nominal A 27L 10000");
}

void testOpposingReplacesNeighbour()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.rules["opposing"] = true;
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "D" && r.rwy == "27L", "opposing D 27L");
}

void testStrategySouth()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "south";
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "B" && r.rwy == "26R" && r.climbvia, "south B 26R climbvia");
}

void testStrategyFallsBackWhenGroupClosed()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "south";
	req.depRwys = { "27L" };
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "A" && r.rwy == "27L", "south closed falls back to A 27L");
}

void testNearestInSouthArea()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "nearest";
	req.position = inSouth;
	check(sidOf(cfg, select(cfg, req)) == "B", "nearest in south -> B");
}

void testNearestInNeutralZone()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "nearest";
	req.position = inNeutral;
	check(areaGroupAt(cfg, inNeutral) == std::optional<std::string>("default"), "default area wins");
	check(areaGroupAt(cfg, inSouth) == std::optional<std::string>("south"), "south area");
	check(!areaGroupAt(cfg, GeoPoint{ 0, 0 }), "outside every area");
	check(sidOf(cfg, select(cfg, req)) == "A", "nearest in neutral zone -> A");
}

void testNearestWithoutPositionIsNominal()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "nearest";
	check(sidOf(cfg, select(cfg, req)) == "A", "nearest without position -> A");
}

void testAtcRunwayBeatsStrategy()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.mode = "south";
	req.atcRwy = "27R";
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "A" && r.rwy == "27R", "ATC rwy 27R -> A 27R");
	req.atcRwy = "09L"; // inactive, not in any DIKOL entry
	check(!select(cfg, req).entryId, "ATC rwy unknown to the entries -> no SID");
}

void testEnginesFilter()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "OKASI");
	check(sidOf(cfg, select(cfg, req)) == "A" && select(cfg, req).initialClimb == 12000, "OKASI jet");
	req.engine = 'P';
	check(!select(cfg, req).entryId, "OKASI is jet only");
}

void testUnknownEngineUsesJetClimb()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.engine = '?';
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "A" && r.initialClimb == 10000, "unknown engine -> jet climb");
	req.engine = 'P';
	check(select(cfg, req).initialClimb == 5000, "piston climb");
}

void testHelicopterSkipped()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.aircraftKind = 'H';
	check(!select(cfg, req).entryId, "helicopter skipped");
	req.aircraftKind = '?';
	check(select(cfg, req).entryId.has_value(), "unknown kind is never skipped");
}

void testEquipFailureFlag()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.equipment = "SDFY";
	const auto r = select(cfg, req);
	check(!r.entryId && r.equipFailure, "no RNAV -> EQUIP");
	req.equipCheck = false;
	check(select(cfg, req).entryId.has_value(), "equipCheck off ignores equipment");
	req = request(cfg, "DIKOL");
	req.equipment = "SDFY";
	req.pbn = "A1";
	check(select(cfg, req).entryId.has_value(), "PBN counts as RNAV");
	req = request(cfg, "OKASI");
	req.engine = 'P';
	req.equipment = "SDFY";
	check(!select(cfg, req).equipFailure, "engine mismatch is not an EQUIP case");
}

void testEquipCodes()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L", "equip": { "W": true, "Z": false } })");
	auto req = request(cfg, "ABC");
	req.equipment = "SDW";
	check(select(cfg, req).entryId.has_value(), "W present, Z absent");
	req.equipment = "SDWZ";
	check(!select(cfg, req).entryId && select(cfg, req).equipFailure, "forbidden Z present");
	req.equipment = "SD";
	check(!select(cfg, req).entryId, "mandatory W missing");
}

void testNonRnav()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L", "equip": { "NON-RNAV": true } })");
	auto req = request(cfg, "ABC");
	req.equipment = "SDY";
	check(select(cfg, req).entryId.has_value(), "plain equipment is NON-RNAV");
	req.equipment = "SDGY";
	check(!select(cfg, req).entryId, "G is RNAV");
	req.equipment = "SDY";
	req.pbn = "B2";
	check(!select(cfg, req).entryId, "PBN is RNAV");
}

void testUnavailableEntrySkipped()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	for (const auto& e : cfg.entries)
		if (e.waypoint == "DIKOL" && e.designator == "A") req.available.erase(e.id);
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "B" && r.rwy == "26R", "A unavailable -> B (next group)");
}

void testActArrRwyAllowAll()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L", "actArrRwy": { "allow": { "all": "27R" } } },
		{ "sid": "B", "rwy": "27L", "actArrRwy": { "deny": { "any": "27R" } } },
		{ "sid": "C", "rwy": "27L", "actArrRwy": { "allow": { "any": "26L,09L" } } })");
	auto req = request(cfg, "ABC");
	req.arrRwys = { "27R", "26L" };
	check(sidOf(cfg, select(cfg, req)) == "A", "allow-all met");
	req.arrRwys = { "26L" };
	check(sidOf(cfg, select(cfg, req)) == "B", "A refused, B denies only 27R");
	req.arrRwys = { "27R" };
	req.depRwys = { "27L" };
	check(sidOf(cfg, select(cfg, req)) == "A", "27R active");
	req.arrRwys = { "09L" };
	check(sidOf(cfg, select(cfg, req)) == "B", "allow-any: 09L active, A refused, B ok");
	req.arrRwys = { "27R", "26L" };
	const auto cfg2 = plain(R"({ "sid": "C", "rwy": "27L", "actArrRwy": { "allow": { "any": "26L,09L" } } },
		{ "sid": "D", "rwy": "27L", "actArrRwy": { "deny": { "all": "27R,26L" } } })");
	check(sidOf(cfg2, select(cfg2, req)) == "C", "deny-all both active refuses D");
	req.arrRwys = { "27R" };
	check(sidOf(cfg2, select(cfg2, req)) == "D", "deny-all only one active, C needs any");
}

void testDestFilter()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L", "dest": { "LFMN": true } },
		{ "sid": "B", "rwy": "27L", "dest": { "LFPO": false } })");
	auto req = request(cfg, "ABC");
	req.dest = "LFMN";
	check(sidOf(cfg, select(cfg, req)) == "A", "LFMN eligible for A");
	req.dest = "LFBO";
	check(sidOf(cfg, select(cfg, req)) == "B", "LFBO not for A, ok for B");
	req.dest = "LFPO";
	check(!select(cfg, req).entryId, "LFPO denied by B, not listed for A");
}

void testWtcFilter()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L", "wtc": "HJ" }, { "sid": "B", "rwy": "27L" })");
	auto req = request(cfg, "ABC");
	req.wtc = "H";
	check(sidOf(cfg, select(cfg, req)) == "A", "heavy -> A");
	req.wtc = "M";
	check(sidOf(cfg, select(cfg, req)) == "B", "medium -> B");
}

void testNoGroupsDeclarationOrder()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27R" }, { "sid": "B", "rwy": "27L,27R" }, { "sid": "C", "rwy": "27L" })");
	auto req = request(cfg, "ABC");
	const auto r = select(cfg, req);
	check(sidOf(cfg, r) == "B" && r.rwy == "27L", "first eligible declared, 27R inactive");
	req.depRwys = { "27R", "27L" };
	check(sidOf(cfg, select(cfg, req)) == "A", "A once 27R is active");
	req.mode = "nearest"; // meaningless without groups
	check(sidOf(cfg, select(cfg, req)) == "A", "mode ignored without groups");
}

void testRuleListAnyActive()
{
	const auto cfg = plain(R"({ "sid": "A", "rwy": "27L" }, { "sid": "R", "rwy": "27L", "rule": ["wlpg", "elpg"] })");
	auto req = request(cfg, "ABC");
	check(sidOf(cfg, select(cfg, req)) == "A", "no rule active -> A");
	req.rules["elpg"] = true;
	check(sidOf(cfg, select(cfg, req)) == "R", "elpg active -> rule entry goes first");
}

void testRwyPicksFirstOfEntryInDepRwys()
{
	const auto cfg = lfpg();
	auto req = request(cfg, "DIKOL");
	req.depRwys = { "27R" };
	check(select(cfg, req).rwy == "27R", "27R when only 27R is active");
}

int main()
{
	testNominalNorth();
	testOpposingReplacesNeighbour();
	testStrategySouth();
	testStrategyFallsBackWhenGroupClosed();
	testNearestInSouthArea();
	testNearestInNeutralZone();
	testNearestWithoutPositionIsNominal();
	testAtcRunwayBeatsStrategy();
	testEnginesFilter();
	testUnknownEngineUsesJetClimb();
	testHelicopterSkipped();
	testEquipFailureFlag();
	testEquipCodes();
	testNonRnav();
	testUnavailableEntrySkipped();
	testActArrRwyAllowAll();
	testDestFilter();
	testWtcFilter();
	testNoGroupsDeclarationOrder();
	testRuleListAnyActive();
	testRwyPicksFirstOfEntryInDepRwys();
	std::cout << "SID engine tests passed\n";
	return 0;
}
