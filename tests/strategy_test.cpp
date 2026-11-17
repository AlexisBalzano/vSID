#include "../strategy.h"
#include "fixtures_lfpg.h"
#include <cstdlib>
#include <iostream>

using namespace vsid::v2;
using Json = nlohmann::ordered_json;

void check(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

AirportConfig lfpg()
{
	AirportConfig config;
	std::string error;
	const bool ok = parseAirport(fixtures::lfpg(), "LFPG", config, error);
	if (!ok) std::cerr << "Unexpected parse error: " << error << '\n';
	check(ok, "config must parse");
	return config;
}

StrategyBook fresh(const AirportConfig& cfg) { StrategyBook book; book.reset(cfg); return book; }

void testResetUsesDefaultStrategy()
{
	const auto cfg = lfpg();
	const auto book = fresh(cfg);
	check(book.active() == "roulage-mini", "default strategy active");
	check(book.modeFor(cfg, "DIKOL") == "nearest", "wildcard applies");
	check(!book.manualOverride(), "no manual override after reset");
}

void testSetNominalClearsAll()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "nominal", error), "nominal accepted");
	check(book.active().empty(), "nominal is empty id");
	check(book.modeFor(cfg, "DIKOL") == "default", "nominal gives default");
	check(book.manualOverride(), "manual override set");
}

void testSetStrategyCaseInsensitive()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "RANUX-DIKOL-SUD", error), "case-insensitive id");
	check(book.active() == "ranux-dikol-sud", "canonical id stored");
	check(book.modeFor(cfg, "DIKOL") == "south", "DIKOL south");
	check(book.modeFor(cfg, "LANVI") == "default", "LANVI default");
	check(!book.setStrategy(cfg, "nope", error) && !error.empty(), "unknown strategy refused");
	check(book.active() == "ranux-dikol-sud", "unchanged after refusal");
}

void testAssignAtomic()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(!book.assign(cfg, { "DIKOL=south", "FOO=north" }, error), "unknown waypoint refused");
	check(book.modeFor(cfg, "DIKOL") == "nearest", "state unchanged");
	check(!book.manualOverride(), "override unchanged");
	check(!book.assign(cfg, { "DIKOL=south", "LANVI=east" }, error), "bad mode refused");
	check(book.modeFor(cfg, "DIKOL") == "nearest", "state unchanged (mode)");
	check(!book.assign(cfg, { "DIKOL" }, error), "missing = refused");
	check(book.assign(cfg, { "dikol=SOUTH" }, error), "case-insensitive accepted");
	check(book.modeFor(cfg, "DIKOL") == "south", "assigned");
}

void testAssignOverridesStrategy()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "ranux-dikol-sud", error), "set");
	check(book.assign(cfg, { "DIKOL=default" }, error), "assign");
	check(book.modeFor(cfg, "DIKOL") == "default", "assign beats strategy");
}

void testAssignClear()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.assign(cfg, { "DIKOL=north" }, error), "assign");
	check(book.assign(cfg, { "clear" }, error), "clear");
	check(book.modeFor(cfg, "DIKOL") == "nearest", "back to strategy");
}

void testManualOverrideBlocksExternal()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	const Json lanvi = Json::parse(R"({"strategy":"lanvi-bubli-baxir-nord"})");
	check(book.setStrategy(cfg, "ranux-dikol-sud", error), "set");
	check(book.applyExternal(cfg, lanvi, error), "ignored returns true");
	check(book.active() == "ranux-dikol-sud", "unchanged while overridden");
	book.resume();
	check(!book.manualOverride(), "resumed");
	check(book.applyExternal(cfg, lanvi, error), "applied");
	check(book.active() == "lanvi-bubli-baxir-nord", "external applied");
	check(book.toBridge(cfg)["source"] == "external", "source external");
}

void testExternalInvalidLeavesStateUnchanged()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(!book.applyExternal(cfg, Json::parse(R"({"strategy":"inconnue"})"), error), "unknown strategy");
	check(!book.applyExternal(cfg, Json::parse(R"({"assign":{"DIKOL":"east"}})"), error), "bad mode");
	check(!book.applyExternal(cfg, Json::parse(R"({"strategy":"ranux-dikol-sud","assign":{"DIKOL":"east"}})"), error), "valid strategy + bad assign");
	check(!book.applyExternal(cfg, Json::parse("42"), error), "non-object");
	check(book.active() == "roulage-mini", "strategy unchanged");
	check(book.modeFor(cfg, "DIKOL") == "nearest", "modes unchanged");
	check(book.toBridge(cfg)["source"] == "manual", "source unchanged");
}

void testExternalAssignRules()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.applyExternal(cfg, Json::parse(R"({"assign":{"DIKOL":"north"}})"), error), "assign alone");
	check(book.active() == "roulage-mini" && book.modeFor(cfg, "DIKOL") == "north", "assign replaces overrides only");
	check(book.applyExternal(cfg, Json::parse(R"({"strategy":"ranux-dikol-sud"})"), error), "strategy alone");
	check(book.modeFor(cfg, "DIKOL") == "south", "strategy clears overrides");
	check(book.applyExternal(cfg, Json::parse("{}"), error), "empty object");
	check(book.active() == "ranux-dikol-sud", "empty object no change");
}

void testAssignAfterExternalIsManual()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.applyExternal(cfg, Json::parse(R"({"strategy":"ranux-dikol-sud"})"), error), "external");
	check(book.toBridge(cfg)["source"] == "external", "source external");
	check(book.assign(cfg, { "DIKOL=north" }, error), "assign");
	check(book.toBridge(cfg)["source"] == "manual" && book.manualOverride(), "assign makes it manual");
	book.resume();
	check(book.applyExternal(cfg, Json::parse(R"({"strategy":"ranux-dikol-sud"})"), error), "external again");
	check(book.toBridge(cfg)["source"] == "external", "external again source");
	check(book.assign(cfg, { "clear" }, error), "clear");
	check(book.toBridge(cfg)["source"] == "manual" && book.manualOverride(), "clear makes it manual");
}

void testExternalStrategyWithAssign()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.assign(cfg, { "LANVI=north" }, error), "assign");
	book.resume();
	check(book.applyExternal(cfg, Json::parse(R"({"strategy":"ranux-dikol-sud","assign":{"PO":"south"}})"), error), "both");
	check(book.active() == "ranux-dikol-sud", "strategy set");
	check(book.modeFor(cfg, "PO") == "south", "assign applied");
	check(book.modeFor(cfg, "LANVI") == "default", "old overrides replaced");
}

void testExternalNominal()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.applyExternal(cfg, Json::parse(R"({"strategy":"nominal"})"), error), "nominal");
	check(book.active().empty() && book.modeFor(cfg, "DIKOL") == "default", "nominal applied");
	check(book.toBridge(cfg)["source"] == "external", "source external");
}

void testExternalInvalidWhileManualOverride()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "ranux-dikol-sud", error), "set");
	check(!book.applyExternal(cfg, Json::parse(R"({"strategy":"inconnue"})"), error), "invalid refused");
	check(!book.applyExternal(cfg, Json::parse("42"), error), "non-object refused");
	check(book.active() == "ranux-dikol-sud" && book.manualOverride(), "state unchanged");
	check(book.toBridge(cfg)["source"] == "manual", "source unchanged");
}

void testToBridge()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "ranux-dikol-sud", error), "set");
	// reduced fixture: ranux-dikol-sud only names DIKOL
	const Json bridge = book.toBridge(cfg);
	check(bridge["strategy"] == "ranux-dikol-sud", "id");
	check(bridge["label"] == "RANUX DIKOL -> Sud", "label");
	check(bridge["source"] == "manual", "source");
	check(bridge["effective"] == Json::parse(R"({"DIKOL":"south"})"), "effective");
	check(bridge["assign"].is_object() && bridge["assign"].empty(), "assign empty");

	check(book.setStrategy(cfg, "nominal", error), "nominal");
	const Json nominal = book.toBridge(cfg);
	check(nominal["strategy"] == "nominal" && nominal["label"] == "Nominal", "nominal labels");
	check(nominal["effective"].empty(), "nominal effective empty");

	check(book.setStrategy(cfg, "roulage-mini", error), "wildcard strategy");
	const Json wild = book.toBridge(cfg);
	check(wild["effective"] == Json::parse(R"({"DIKOL":"nearest","OKASI":"nearest","LANVI":"nearest","PO":"nearest"})"),
		"wildcard expanded per point, no '*' key");

	check(book.assign(cfg, { "PO=default", "LANVI=south" }, error), "override under the wildcard");
	const Json overridden = book.toBridge(cfg);
	check(!overridden["effective"].contains("PO"), "point overridden to default is missing");
	check(!overridden["effective"].contains("*"), "still no '*' key");
	check(overridden["effective"]["LANVI"] == "south" && overridden["effective"]["DIKOL"] == "nearest", "other points resolved");
}

void testPruneInvalidAfterReload()
{
	const auto cfg = lfpg();
	auto book = fresh(cfg);
	std::string error;
	check(book.setStrategy(cfg, "ranux-dikol-sud", error), "set");
	check(book.assign(cfg, { "PO=north", "LANVI=south", "OKASI=nearest" }, error), "assign");

	// reloaded config: PO gone, group south renamed
	auto json = fixtures::lfpg();
	json["procedures"].erase("PO");
	json["selection"].erase("PO");
	json["runwayGroups"]["sud"] = json["runwayGroups"]["south"];
	json["runwayGroups"].erase("south");
	json["areas"]["south"]["group"] = "sud";
	json["selection"]["DIKOL"] = Json::array({ "north", "sud" });
	json["selection"]["LANVI"] = Json::array({ "sud", "north" });
	json["strategies"]["ranux-dikol-sud"]["assign"]["DIKOL"] = "sud";
	AirportConfig reloaded;
	check(parseAirport(json, "LFPG", reloaded, error), "reloaded config parses");

	const auto dropped = book.pruneInvalid(reloaded);
	check((dropped == std::vector<std::string>{ "LANVI=south", "PO=north" }), "unknown waypoint and unknown mode dropped");
	check(book.modeFor(reloaded, "OKASI") == "nearest", "valid pair kept");
	check(book.modeFor(reloaded, "LANVI") == "default", "dropped pair falls back to the strategy");
	check(book.active() == "ranux-dikol-sud" && book.manualOverride(), "strategy and override state kept");
	const Json bridge = book.toBridge(reloaded);
	check(bridge["assign"] == Json::parse(R"({"OKASI":"nearest"})"), "bridge assign pruned");
	check(book.pruneInvalid(reloaded).empty(), "nothing left to drop");
	check(fresh(cfg).pruneInvalid(cfg).empty(), "nothing dropped on the same config");
}

int main()
{
	testResetUsesDefaultStrategy();
	testSetNominalClearsAll();
	testSetStrategyCaseInsensitive();
	testAssignAtomic();
	testAssignOverridesStrategy();
	testAssignClear();
	testManualOverrideBlocksExternal();
	testExternalInvalidLeavesStateUnchanged();
	testExternalAssignRules();
	testAssignAfterExternalIsManual();
	testExternalStrategyWithAssign();
	testExternalNominal();
	testExternalInvalidWhileManualOverride();
	testToBridge();
	testPruneInvalidAfterReload();
	std::cout << "Strategy tests passed\n";
	return 0;
}
