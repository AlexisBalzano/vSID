#include "../sidconfigv2.h"
#include "fixtures_lfpg.h"
#include <cmath>
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
	check(ok, "fixture must parse");
	return config;
}

// returns the error text, empty if the file was accepted
std::string parseError(const Json& json)
{
	AirportConfig config;
	std::string error;
	if (parseAirport(json, "LFPG", config, error)) return "";
	return error.empty() ? "(empty error)" : error;
}

std::vector<const Entry*> entriesOf(const AirportConfig& config, const std::string& waypoint)
{
	std::vector<const Entry*> result;
	for (const auto& e : config.entries) if (e.waypoint == waypoint) result.push_back(&e);
	return result;
}

bool contains(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

void testParsesDikol()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	check(config.icao == "LFPG", "icao kept");
	const auto dikol = entriesOf(config, "DIKOL");
	check(dikol.size() == 4, "four DIKOL entries");
	check(dikol[0]->designator == "A" && dikol[1]->designator == "D"
		&& dikol[2]->designator == "B" && dikol[3]->designator == "E", "file order A, D, B, E");
	check(dikol[1]->rules == std::vector<std::string>{"opposing"}, "D rule opposing");
	check(dikol[0]->rules.empty(), "A has no rule");
	check(dikol[2]->climbvia && !dikol[0]->climbvia, "B climbvia only");
	check((dikol[0]->rwys == std::vector<std::string>{"27L", "27R"}), "rwys keep file order");
	check((dikol[2]->rwys == std::vector<std::string>{"26R", "26L"}), "rwys keep file order (26R first)");
	for (size_t i = 0; i < config.entries.size(); ++i) check(config.entries[i].id == static_cast<int>(i), "ids follow file order");
	check(config.customRules.at("opposing") == false, "custom rule initial value");
}

void testDefaultsResolved()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	const Entry& a = *entriesOf(config, "DIKOL")[0];
	check(a.initialClimb.at('J') == 10000 && a.initialClimb.at('P') == 5000 && a.initialClimb.at('T') == 5000, "climb from defaults");
	check(a.equip.size() == 1 && a.equip.at("RNAV"), "equip from defaults");
	check(a.engines.empty() && a.wtc.empty(), "engines and wtc open");
}

void testNumberClimbAndEngines()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	const Entry& okasi = *entriesOf(config, "OKASI")[0];
	check(okasi.engines == "J", "engines J");
	check(okasi.initialClimb.at('J') == 12000, "J overridden");
	check(okasi.initialClimb.at('P') == 5000 && okasi.initialClimb.at('T') == 5000, "P and T from defaults");

	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["PO"][0]["initialClimb"] = 7000;
	const Entry& po = *entriesOf(parseOk(json), "PO")[0];
	check(po.initialClimb.at('J') == 7000 && po.initialClimb.at('P') == 7000 && po.initialClimb.at('T') == 7000, "number applies to all engines");

	json = vsid::v2::fixtures::lfpg();
	json["defaults"].erase("initialClimb");
	const Entry& capped = *entriesOf(parseOk(json), "PO")[0];
	check(capped.initialClimb.at('J') == 12000 && capped.initialClimb.at('T') == 12000, "maxInitialClimb when nothing else");
}

void testExplicitEmptyEquip()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["PO"][0]["equip"] = Json::object();
	const auto config = parseOk(json);
	check(entriesOf(config, "PO")[0]->equip.empty(), "explicit {} overrides defaults");
	check(!entriesOf(config, "LANVI")[0]->equip.empty(), "other entries keep defaults");
}

void testLocalDefaults()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["PO"] = Json{ { "defaults", { { "initialClimb", 6000 } } }, { "entries", json["procedures"]["PO"] } };
	const Entry& po = *entriesOf(parseOk(json), "PO")[0];
	check(po.initialClimb.at('J') == 6000, "point defaults override airport defaults");
	check(po.equip.at("RNAV"), "unset point default falls back to airport defaults");
}

void testCoordinate()
{
	const auto north = parseCoordinate("N049.00.26.119");
	check(north.has_value() && std::fabs(*north - 49.007255) < 1e-6, "north coordinate");
	const auto south = parseCoordinate("S001.00.00.000");
	check(south.has_value() && *south < 0, "south negative");
	const auto west = parseCoordinate("W002.30.00.000");
	check(west.has_value() && std::fabs(*west + 2.5) < 1e-9, "west negative");
	check(!parseCoordinate("bad").has_value(), "bad text");
	check(!parseCoordinate("").has_value(), "empty text");
	check(!parseCoordinate("N049.00.26").has_value(), "missing part");
	check(!parseCoordinate("X049.00.26.119").has_value(), "bad hemisphere");
}

void testAreasOrdered()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	check(config.areas.size() == 3, "three areas");
	check(config.areas[0].name == "north" && config.areas[1].name == "south" && config.areas[2].name == "t2-mid", "declaration order");
	check(config.areas[0].group == "north" && config.areas[2].group == "default", "groups");
	check(config.areas[0].points.size() == 4 && config.areas[2].points.size() == 3, "point counts");
	check(std::fabs(config.areas[0].points[0].lat - 49.0121) < 1e-3, "first point converted");
}

void testStrategies()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	check(config.findStrategy("RANUX-DIKOL-SUD") != nullptr, "case-insensitive lookup");
	check(config.findStrategy("nope") == nullptr, "unknown strategy");
	check(config.defaultStrategy == "roulage-mini", "default strategy");
	const Strategy* s = config.findStrategy("roulage-mini");
	check(s && s->label == "Roulage mini" && s->assign.size() == 1 && s->assign[0].first == "*" && s->assign[0].second == "nearest", "assign kept");
	check(config.strategies.size() == 3 && config.strategies[0].id == "ranux-dikol-sud", "declaration order");
	check(config.selection.at("DIKOL") == (std::vector<std::string>{"north", "south"}), "selection order");
}

void testRejectsV1()
{
	auto json = vsid::v2::fixtures::lfpg();
	json.erase("version");
	check(contains(parseError(json), "version"), "missing version");
	json["version"] = 1;
	check(contains(parseError(json), "version"), "version 1");
	// the plugin log already says "[LFPG] config v2 rejected: <reason>"
	check(parseError(json).rfind("config version 2 required", 0) == 0, "reason without the ICAO prefix");
	json["version"] = 2;
	json["maxInitialClimb"] = "high";
	check(parseError(json).rfind("malformed value", 0) == 0, "malformed value without the ICAO prefix");
}

void testRejectsUnknownGroup()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["selection"]["DIKOL"] = Json::array({ "east" });
	check(contains(parseError(json), "unknown group"), "selection group");

	json = vsid::v2::fixtures::lfpg();
	json["areas"]["north"]["group"] = "east";
	check(contains(parseError(json), "unknown group"), "area group");

	json = vsid::v2::fixtures::lfpg();
	json["strategies"]["roulage-mini"]["assign"]["*"] = "east";
	check(contains(parseError(json), "unknown group"), "strategy group");
}

void testRejectsUnknownWaypoint()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["selection"]["NOPE"] = Json::array({ "north" });
	check(contains(parseError(json), "unknown waypoint"), "selection waypoint");
	json = vsid::v2::fixtures::lfpg();
	json["strategies"]["roulage-mini"]["assign"]["NOPE"] = "north";
	check(contains(parseError(json), "unknown waypoint"), "assign waypoint");
}

void testRejectsUnknownRule()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["DIKOL"][1]["rule"] = "foo";
	check(contains(parseError(json), "unknown rule"), "unknown rule");
	json["procedures"]["DIKOL"][1]["rule"] = Json::array({ "opposing", "foo" });
	check(contains(parseError(json), "unknown rule"), "unknown rule in list");
	json["procedures"]["DIKOL"][1]["rule"] = Json::array({ "opposing" });
	check(parseError(json).empty(), "rule list accepted");
}

void testRejectsBadEntry()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["PO"][0]["sid"] = "";
	check(contains(parseError(json), "designator"), "empty sid");
	json = vsid::v2::fixtures::lfpg();
	json["procedures"]["PO"][0]["rwy"] = "26R,99";
	check(contains(parseError(json), "unknown runway"), "runway not in runways");
}

void testRejectsBadArea()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["areas"]["north"]["points"].erase(2);
	json["areas"]["north"]["points"].erase(1);
	check(contains(parseError(json), "area"), "two points");
	json = vsid::v2::fixtures::lfpg();
	json["areas"]["north"]["points"][0]["lat"] = "garbage";
	check(contains(parseError(json), "area"), "bad coordinate");
}

void testRejectsStrategyId()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["strategies"]["ranux dikol"] = json["strategies"]["roulage-mini"];
	check(contains(parseError(json), "strategy id"), "space in id");
	json = vsid::v2::fixtures::lfpg();
	json["strategies"]["nominal"] = json["strategies"]["roulage-mini"];
	check(contains(parseError(json), "strategy id"), "nominal reserved");
	json = vsid::v2::fixtures::lfpg();
	json["strategies"]["Resume"] = json["strategies"]["roulage-mini"];
	check(contains(parseError(json), "strategy id"), "resume reserved");
	json = vsid::v2::fixtures::lfpg();
	json["defaultStrategy"] = "nope";
	check(contains(parseError(json), "defaultStrategy"), "unknown default strategy");
	json = vsid::v2::fixtures::lfpg();
	json["strategies"]["roulage-mini"]["assign"]["*"] = 3;
	check(contains(parseError(json), "mode"), "invalid mode");
}

void testEntryArea()
{
	auto json = vsid::v2::fixtures::lfpg();
	json["procedures"]["DIKOL"][0]["area"] = "north";
	json["procedures"]["DIKOL"][2]["area"] = Json::array({ "south", "t2-mid" });
	const auto config = parseOk(json);
	const auto dikol = entriesOf(config, "DIKOL");
	check(dikol[0]->areas == std::vector<std::string>{"north"}, "area string");
	check((dikol[2]->areas == std::vector<std::string>{"south", "t2-mid"}), "area list keeps order");
	check(dikol[1]->areas.empty(), "no area = no condition");

	json["procedures"]["DIKOL"][0]["area"] = "nope";
	check(contains(parseError(json), "area"), "unknown area refused");
	json["procedures"]["DIKOL"][0]["area"] = 3;
	check(contains(parseError(json), "area"), "area must be a string or a list");
	json["procedures"]["DIKOL"][0]["area"] = Json::array({ "north", 1 });
	check(contains(parseError(json), "area"), "area list must hold strings");
}

void testGroupOf()
{
	const auto config = parseOk(vsid::v2::fixtures::lfpg());
	check(config.groupOf("26R") == "south", "26R south");
	check(config.groupOf("09L") == "north", "09L north");
	check(config.groupOf("XX").empty(), "unknown runway");
}

int main()
{
	testParsesDikol();
	testDefaultsResolved();
	testNumberClimbAndEngines();
	testExplicitEmptyEquip();
	testLocalDefaults();
	testCoordinate();
	testAreasOrdered();
	testStrategies();
	testRejectsV1();
	testRejectsUnknownGroup();
	testRejectsUnknownWaypoint();
	testRejectsUnknownRule();
	testRejectsBadEntry();
	testRejectsBadArea();
	testRejectsStrategyId();
	testEntryArea();
	testGroupOf();
	std::cout << "Config v2 parser tests passed\n";
	return 0;
}
