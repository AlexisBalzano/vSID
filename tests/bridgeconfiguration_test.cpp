#include "../bridgeconfiguration.h"
#include "../configurationrefresh.h"
#include <cstdlib>
#include <iostream>
#include <sstream>

void check(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Flight
{
	std::string callsign;
	std::string origin;
	bool valid = true;
	bool cleared = false;
	bool IsValid() const { return this->valid; }
	bool GetClearenceFlag() const { return this->cleared; }
	const Flight& GetFlightPlanData() const { return *this; }
	const char* GetOrigin() const { return this->origin.c_str(); }
};

void testRefresh()
{
	std::map<std::string, Flight> flights{
		{"AUTO", {"AUTO", "EGKK"}},
		{"CLEARED", {"CLEARED", "EGKK", true, true}},
		{"MANUAL", {"MANUAL", "EGLC"}},
		{"OTHER", {"OTHER", "EGLL"}},
		{"INVALID", {"INVALID", "EGKK", false}}
	};
	std::map<std::string, int> processed{{"AUTO", 42}, {"CLEARED", 2}, {"MANUAL", 3}, {"OTHER", 4}, {"INVALID", 5}};
	std::map<std::string, int> saved;
	std::set<std::string> rechecked;
	vsid::configurationrefresh::refresh(processed,
		[&](const std::string& callsign) { return flights.at(callsign); },
		[](std::string origin) { return origin == "EGKK" || origin == "EGLC"; },
		[](std::string origin) { return origin == "EGKK" || origin == "EGLL"; },
		[&](const std::string& callsign, int info)
		{
			check(processed.contains(callsign), "save flight information before erasing the processed entry");
			saved[callsign] = info;
		},
		[&](const Flight& flight)
		{
			check(!processed.contains("AUTO"), "invalidate automode flights before rechecking suggestions");
			rechecked.insert(flight.callsign);
		});
	check(!processed.contains("AUTO") && saved == std::map<std::string, int>{{"AUTO", 42}}, "affected uncleared automode flight saved and queued for reassignment");
	check(processed.size() == 4 && processed.contains("CLEARED") && processed.contains("MANUAL") &&
		processed.contains("OTHER") && processed.contains("INVALID"), "cleared, manual, unaffected and invalid flights retained");
	check(rechecked == std::set<std::string>{"CLEARED", "MANUAL"}, "only remaining valid affected flights get suggestion refreshes");
}

int main()
{
	testRefresh();
	using namespace vsid::bridgeconfig;
	using Json = nlohmann::json;
	Snapshot snapshot;
	snapshot.rules = {{"LFPB",{{"OPPOSING",true}}},{"LFOB",{{"PGEAST",true}}},{"EGKK",{{"ANY:KEY",false}}}};
	snapshot.areas = {{"EGKK",{{"AREA\"NAME",true}}}};
	snapshot.states["LFOB"] = {vsid::autoconfig::Status::Matched, "East|quoted\" profile", {}};
	auto fields = serialize(snapshot);
	check(Json::parse(fields.at("rules")).at("LFPB").at("OPPOSING") == true, "LFPB published generically");
	check(Json::parse(fields.at("rules")).at("LFOB").size() == 1, "LFOB publishes actual PGEAST without invented regional rules");
	check(Json::parse(fields.at("areas")).at("EGKK").at("AREA\"NAME") == true, "arbitrary areas escaped");
	check(Json::parse(fields.at("autoconfig")).at("LFOB").at("profile") == "East|quoted\" profile", "profiles safely escaped");
	check(serialize({}).at("rules") == "{}", "empty snapshot explicitly clears inactive airports");
	vsid::autoconfig::Controller controller;
	std::istringstream mapping(R"({"version":2,"references":{"EGLL":{"groups":{"east":["09L"]}}},"profiles":[{"name":"East","when":{"EGLL":"east"},"enable":{"EGKK":["east"]}}]})");
	std::string error;
	check(controller.load(mapping, error), "load lowercase bridge status fixture");
	vsid::autoconfig::Rules airportRules{{"east", false}};
	controller.apply("egkk", {{"EGLL", {{"09L"}, {}}}}, airportRules);
	Snapshot lowercaseAirport;
	lowercaseAirport.states["EGKK"] = controller.stateFor("egkk");
	const auto state = Json::parse(serialize(lowercaseAirport).at("autoconfig")).at("EGKK");
	check(state.at("status") == "MATCHED" && state.at("profile") == "East", "lowercase sector airport publishes matched status instead of NOT_LOADED");
	std::map<int, std::string> cache;
	int writes = 0;
	auto writer = [&](const std::string&) { ++writes; return true; };
	check(publish(1, "EGKK=1;", 4096, cache, writer) == Publication::Published, "initial publication");
	check(publish(1, std::string(4097,'X'), 4096, cache, writer) == Publication::Overflow && writes == 1 && cache.at(1) == "EGKK=1;",
		"overflow retains previous bridge value without publishing empty");
	check(publish(1, "EGKK=1;", 4096, cache, writer) == Publication::Unchanged && writes == 1, "cache suppresses duplicate");
	check(publish(1, "", 4096, cache, writer) == Publication::Published && writes == 2, "real empty snapshot still clears");
	check(publish(1, "retry", 4096, cache, [](const auto&) {return false;}) == Publication::Failed && cache.at(1).empty(), "failed write retried");
	std::map<std::string,bool> rules{{"ONE",false},{"TWO",true}};
	std::vector<std::string> selected;
	check(!assign(rules, std::vector<std::string>{"ONE=on","MISSING=off"}, selected) && !rules.at("ONE") && selected.empty(), "unknown assignment atomic");
	check(!assign(rules, std::vector<std::string>{"ONE=on","one=off"}, selected), "duplicate assignment rejected");
	check(assign(rules, std::vector<std::string>{"one=on","two=0"}, selected) && rules.at("ONE") && !rules.at("TWO"), "generic explicit assignments");
	std::cout << "Generic bridge tests passed\n";
}
