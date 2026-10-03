#include "../autoconfiguration.h"
#include "../include/nlohmann/json.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace vsid::autoconfig;
using Json = nlohmann::json;

void check(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

bool load(Controller& controller, const Json& document)
{
	std::istringstream input(document.dump());
	std::string error;
	const bool loaded = controller.load(input, error);
	if (!loaded) std::cerr << "Expected validation diagnostic: " << error << '\n';
	return loaded;
}

Json configuration()
{
	return Json::parse(R"({
		"version":2,
		"references":{"EGLL":{"groups":{"east":["09L","09R"],"west":["27L","27R"]}}},
		"airportGroups":{"REGIONAL":["EGKK","EGLC"]},
		"profiles":[
			{"name":"East","when":{"EGLL":"east"},"enable":{"REGIONAL":["east"]}},
			{"name":"West","when":{"EGLL":"west"},"enable":{"REGIONAL":["west"]}}
		]
	})");
}

void testOverrides()
{
	Controller controller;
	check(load(controller, configuration()), "load generic mapping");
	Rules rules{{"east", false}, {"west", true}, {"night", false}};
	Snapshot east{{"EGLL", {{"09L"}, {"09R"}}}};
	check(controller.apply("EGKK", east, rules) && rules.at("east") && !rules.at("west"), "default both + exclusive enable arrays");
	rules.at("night") = true;
	check(!controller.rememberRuleChange("egkk", "NIGHT", rules) && !controller.isManual("EGKK"), "unmanaged toggle never creates override");
	Snapshot west{{"EGLL", {{}, {"27L"}}}};
	check(controller.apply("EGKK", west, rules) && rules.at("west") && rules.at("night"), "unmanaged toggle leaves detection running");
	rules["east"] = true;
	rules["west"] = false;
	check(controller.rememberRuleChange("egkk", "EAST", rules) && controller.isManual("EGKK"), "managed toggle creates override");
	auto reactivated = Rules{{"EAST", false}, {"WEST", true}, {"night", false}};
	check(controller.apply("EGKK", west, reactivated) && reactivated.at("EAST") && !reactivated.at("WEST") && !reactivated.at("night"),
		"manual restore changes managed keys only, preserving new unrelated defaults");
	controller.enabled = false;
	reactivated["EAST"] = false;
	check(!controller.apply("EGKK", west, reactivated) && !reactivated.at("EAST"), "OFF prevents override replay");
	check(controller.rememberRuleChange("EGKK", "east", reactivated), "editing an existing override while OFF updates its managed value");
	controller.resume("EGKK");
	check(!controller.enabled && !controller.isManual("EGKK"), "airport resume does not enable global Auto");
	check(!controller.rememberRuleChange("EGKK", "east", reactivated) && !controller.isManual("EGKK"), "OFF toggle creates no hidden override");
	controller.enabled = true;
	check(controller.apply("EGKK", east, reactivated) && reactivated.at("EAST"), "later ON resumes detection");
	check(controller.setManual("EGKK", reactivated), "explicit manual command");
	check(controller.setManual("EGLC", reactivated), "second manual airport");
	controller.enabled = false;
	controller.resumeAll();
	check(!controller.enabled && controller.manualAirports().empty(), "resume all clears active and inactive overrides without enabling Auto");

	controller.enabled = true;
	check(controller.setManual("EGKK", reactivated), "snapshot for reload");
	auto changed = configuration();
	changed["profiles"][0]["enable"]["REGIONAL"] = Json::array({"other"});
	check(load(controller, changed), "managed-key change on reload");
	reactivated["EAST"] = false;
	reactivated["other"] = true;
	controller.apply("EGKK", east, reactivated);
	check(!reactivated.at("EAST") && reactivated.at("other"), "removed keys never restored after successful reload");
}

void testValidationAndDetection()
{
	Controller controller;
	auto base = configuration();
	check(load(controller, base), "valid mapping");
	Rules rules{{"east", false}, {"west", false}};
	Snapshot east{{"EGLL", {{"09L"}, {}}}};
	auto reject = [&](const Json& document)
	{
		check(!load(controller, document), "invalid mapping rejected");
		check(controller.detect("EGKK", east, rules).ready, "failed reload preserves working mapping");
	};
	for (int version : {0, 1, 3}) { auto invalid = base; invalid["version"] = version; reject(invalid); }
	auto invalid = base; invalid["profiles"][0]["enable"]["REGIONAL"] = {{"east",true}}; reject(invalid);
	invalid = base; invalid["profiles"][0]["enable"]["REGIONAL"] = Json::array({"east","EAST"}); reject(invalid);
	invalid = base; invalid["profiles"][0]["enable"]["EGKK"] = Json::array(); reject(invalid);
	invalid = base; invalid["profiles"][0]["enable"]["UNKNOWN_GROUP"] = Json::array(); reject(invalid);
	invalid = base; invalid["airportGroups"]["REGIONAL"] = Json::array({"EGKK","egkk"}); reject(invalid);
	invalid = base; invalid["airportGroups"]["EGKK"] = Json::array({"EGLC"}); reject(invalid);
	invalid = base; invalid["profiles"][0]["when"]["EGLL"] = "typo"; reject(invalid);
	invalid = base; invalid["references"]["EGLL"]["groups"]["west"] = Json::array({"09L"}); reject(invalid);
	invalid = base; invalid["references"]["EGLL"]["source"] = "departure"; reject(invalid);
	invalid = base; invalid["profiles"][0]["fallback"] = 1; reject(invalid);
	invalid = base; invalid["enabledByDefault"] = "true"; reject(invalid);
	invalid = base; invalid["unexpected"] = true; reject(invalid);
	for (const auto text : {"{", "{\"version\":2,\"version\":2}", "null"})
	{
		std::istringstream input(text);
		std::string error;
		check(!controller.load(input, error) && controller.loaded(), "duplicate/malformed JSON rejected atomically");
	}
	auto document = base;
	document["profiles"][1]["enable"]["REGIONAL"] = Json::array();
	check(load(controller, document), "empty enable array accepted");
	rules = {{"east",true},{"unmanaged",true}};
	check(controller.apply("EGKK", {{"EGLL",{{},{"27L"}}}}, rules) && !rules.at("east") && rules.at("unmanaged"), "empty array clears union only");
	document["profiles"][1]["enable"].erase("REGIONAL");
	document["profiles"][1]["enable"]["EGNX"] = Json::array({"other"});
	check(load(controller, document), "different targets may have different unions");
	check(!controller.detect("EGKK", {{"EGLL",{{},{"27L"}}}}, rules).ready, "omitted airport never implicitly cleared");

	check(load(controller, base), "reset");
	check(controller.detect("EGKK", east, {{"east",false}}).state.code == Status::MissingRule, "missing rule caught at runtime");
	check(controller.detect("ZZZZ", east, rules).state.code == Status::Unmanaged, "unmanaged airport");
	check(controller.detect("EGKK", {}, {{"east",false},{"west",true}}).state.code == Status::Undetermined, "missing reference");
	Snapshot conflict{{"EGLL",{{"09L"},{"27R"}}}};
	check(!controller.detect("EGKK", conflict, {{"east",false},{"west",true}}).ready, "conflicting roles retained");
	document = base; document["references"]["EGLL"]["source"] = "arrivals";
	check(load(controller, document) && controller.detect("EGKK", conflict, {{"east",false},{"west",true}}).ready, "arrival-only selection");
	document = base; document["profiles"][1]["when"]["EGLL"] = "east";
	check(load(controller, document) && controller.detect("EGKK", east, {{"east",false},{"west",true}}).state.code == Status::Ambiguous, "ties never use list order");

	Controller startup;
	document = base; document["enabledByDefault"] = false;
	check(load(startup, document) && !startup.enabled, "startup flag applied");
	check(load(startup, base) && !startup.enabled, "reload preserves global OFF");
	check(controller.missingRunways({{"EGLL",{"09L","09R","27L","27R"}}}).empty(), "valid runway inventory");
	check(controller.missingRunways({{"EGLL",{"09L"}}}).size() == 3, "missing group runways detected");
}

void testFallback()
{
	auto config = configuration();
	config["references"]["EGNX"] = config["references"]["EGLL"];
	for (auto& profile : config["profiles"]) profile["when"]["EGNX"] = "east";
	config["profiles"].push_back({{"name","Missing secondary runway"},{"fallback",true},
		{"when",{{"EGLL","east"}}},{"enable",{{"REGIONAL",Json::array({"west"})}}}});
	Controller controller;
	check(load(controller, config), "explicit fallback");
	Rules rules{{"east", false}, {"west", false}};
	Snapshot snapshot{{"EGLL",{{},{"09L"}}}};
	check(controller.apply("EGKK", snapshot, rules) && rules.at("west"), "fallback used for absent secondary selections");
	snapshot["EGNX"] = {{},{"09L"}};
	check(controller.apply("EGKK", snapshot, rules) && rules.at("east") && !rules.at("west"), "normal match takes precedence");
	snapshot["EGNX"] = {{"09L"},{"27L"}};
	check(!controller.detect("EGKK", snapshot, rules).ready, "fallback cannot mask conflicting runways");
	snapshot.erase("EGNX");
	config["profiles"].push_back({{"name","Another possible fallback"},{"fallback",true},
		{"when",{{"EGLL","east"},{"EGNX","west"}}},{"enable",{{"REGIONAL",Json::array({"east"})}}}});
	check(load(controller, config) && !controller.detect("EGKK", snapshot, rules).ready,
		"missing data for a competing fallback cannot be guessed");
}

void testExternal(const char* path)
{
	Controller controller;
	std::ifstream input(path);
	std::string error;
	check(controller.load(input, error), error.c_str());
	const Rules regional{{"wlpg",false},{"wipg",false},{"elpg",false},{"eipg",false}};
	for (const auto pg : {"26R","08L"})
		for (const auto po : {"25","07"})
		{
			const bool west = std::string(pg) == "26R", linked = west == (std::string(po) == "25");
			const std::string expected = west ? (linked ? "wlpg" : "wipg") : (linked ? "elpg" : "eipg");
			Snapshot snapshot{{"LFPG",{{},{pg}}},{"LFPO",{{po},{}}}};
			for (const auto icao : {"LFPN","LFPT","LFPV"})
			{
				auto rules = regional;
				check(controller.apply(icao, snapshot, rules) && rules.at(expected), "external regional combinations");
				int enabled = 0;
				for (const auto& [key,value] : rules) enabled += value;
				check(enabled == 1, "external regional exclusivity");
			}
			for (const auto icao : {"LFPG","LFPO","LFPB"})
			{
				Rules rules{{"opposing",false}};
				controller.apply(icao, snapshot, rules);
				check(rules.at("opposing") == !linked, "external main airports including LFPB");
			}
			Rules rules{{"pgeast",false}};
			snapshot.erase("LFPO");
			controller.apply("LFOB", snapshot, rules);
			check(rules.at("pgeast") == !west, "external LFOB only requires PGEAST and LFPG");
		}
}

int main(int argc, char** argv)
{
	testOverrides();
	testValidationAndDetection();
	testFallback();
	if (argc == 2) testExternal(argv[1]);
	std::cout << "Auto configuration tests passed\n";
}
