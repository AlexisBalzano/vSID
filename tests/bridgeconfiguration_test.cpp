#include "../bridgeconfiguration.h"
#include <cstdlib>
#include <iostream>

void check(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
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
