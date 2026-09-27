#include "../autoconfiguration.h"

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include "../include/nlohmann/json.hpp"

using namespace vsid::autoconfig;

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void testJsonConfiguration()
{
    using Json = nlohmann::json;
    const auto base = Json::parse(R"({
      "version": 1,
      "references": {
        "egll": {"source": "departures", "groups": {"alpha": ["09L"], "bravo": ["27R"]}}
      },
      "profiles": [
        {"name": "Custom alpha", "when": {"EGLL": "alpha"}, "apply": {"egkk": {"custom": true}}},
        {"name": "Custom bravo", "when": {"EGLL": "bravo"}, "apply": {"egkk": {"custom": false}}}
      ]
    })");
    Controller controller;
    controller.enabled = true;
    Rules rules{{"CUSTOM", false}, {"unrelated", true}};
    Snapshot snapshot{{"EGLL", {{"27R"}, {"09L"}}}};
    check(!controller.apply("EGKK", snapshot, rules) && !rules.at("CUSTOM"), "no hard-coded fallback without JSON");
    std::string error;
    auto load = [&](const Json& document) {
        std::istringstream stream(document.dump());
        return controller.load(stream, error);
    };
    check(load(base), error.c_str());
    check(controller.observes("egll") && !controller.observes("LFPG"), "references come exclusively from JSON");
    check(controller.apply("EGKK", snapshot, rules) && rules.at("CUSTOM") && rules.at("unrelated"),
        "arbitrary airports, rule names, and departure-only matching");
    check(controller.statuses().at("EGKK") == "Custom alpha", "profile display name comes from JSON");

    auto modified = base;
    modified["profiles"][0]["apply"]["egkk"]["custom"] = false;
    check(load(modified) && controller.apply("EGKK", snapshot, rules) && !rules.at("CUSTOM"),
        "editing only JSON changes the selected rules");
    check(load(base), error.c_str());
    auto reject = [&](const Json& invalid) {
        check(!load(invalid) && !error.empty(), "invalid mapping rejected with error");
        check(controller.detect("EGKK", snapshot, rules).rules.at("CUSTOM"), "failed reload retains last valid mapping");
    };
    modified = base; modified["version"] = 2; reject(modified);
    modified = base; modified["enabledByDefault"] = "true"; reject(modified);
    modified = base; modified["references"]["egll"]["source"] = "departure"; reject(modified);
    modified = base; modified["references"]["egll"]["groups"]["alpha"] = Json::array({9}); reject(modified);
    modified = base; modified["references"]["egll"]["groups"]["alpha"] = Json::array({"99"}); reject(modified);
    modified = base; modified["references"]["egll"]["groups"]["bravo"] = Json::array({"09L"}); reject(modified);
    modified = base; modified["references"]["egll"]["ignore"] = Json::array({"09L"}); reject(modified);
    modified = base; modified["profiles"][0]["when"]["EGLL"] = "typo"; reject(modified);
    modified = base; modified["profiles"][0]["when"]["XXXX"] = "alpha"; reject(modified);
    modified = base; modified["profiles"][0]["apply"]["egkk"]["custom"] = "true"; reject(modified);
    modified = base; modified["profiles"][0]["apply"]["egkk"]["additional"] = true; reject(modified);
    modified = base; modified["profiles"][0]["unknownField"] = true; reject(modified);
    modified = base; modified["profiles"] = Json::array(); reject(modified);
    modified = base; modified["profiles"][0]["when"] = Json::object(); reject(modified);
    modified = base; modified["profiles"][1]["name"] = "CUSTOM ALPHA"; reject(modified);
    for (const auto* invalid : {"{", "{\"version\":1,\"version\":1}", "null"})
    {
        std::istringstream stream(invalid);
        check(!controller.load(stream, error), "malformed or duplicate-key JSON rejected");
        check(controller.detect("EGKK", snapshot, rules).ready, "parse error leaves working mapping intact");
    }
    std::istringstream unreadable;
    unreadable.setstate(std::ios::failbit);
    check(!controller.load(unreadable, error) && controller.loaded(), "unreadable file preserves last configuration");

    controller.setManual("EGKK", rules);
    check(load(base), error.c_str());
    check(controller.isManual("EGKK") && !controller.apply("EGKK", snapshot, rules), "reload preserves manual overrides");
    controller.resume("EGKK");
    modified = base;
    modified["references"]["egll"]["source"] = "arrivals";
    check(load(modified) && !controller.detect("EGKK", snapshot, rules).rules.at("CUSTOM"), "arrival-only matching configurable");
    modified["references"]["egll"]["source"] = "both";
    check(load(modified) && !controller.detect("EGKK", snapshot, rules).ready, "both roles reject conflicting selections");
    modified = base;
    modified["profiles"][1]["when"]["EGLL"] = "alpha";
    check(load(modified), error.c_str());
    check(!controller.apply("EGKK", snapshot, rules) &&
        controller.statuses().at("EGKK").find("AMBIGUOUS") != std::string::npos, "overlapping profiles never use list order");
    modified = base;
    modified["profiles"].erase(0);
    check(load(modified) && !controller.detect("EGKK", snapshot, rules).ready, "no matching profile retains rules");
    modified = base;
    modified["references"]["EGLC"] = modified["references"]["egll"];
    modified["profiles"][1]["when"] = {{"EGLL", "alpha"}, {"EGLC", "alpha"}};
    check(load(modified) && !controller.detect("EGKK", snapshot, rules).ready,
        "incomplete competing profile does not permit a guess");

    Controller startup;
    modified = base;
    modified["enabledByDefault"] = false;
    std::istringstream firstLoad(modified.dump());
    check(startup.load(firstLoad, error) && !startup.enabled, "JSON can explicitly disable startup Auto");
    modified["enabledByDefault"] = true;
    std::istringstream reload(modified.dump());
    check(startup.load(reload, error) && !startup.enabled, "reload preserves session OFF despite startup preference");
    Controller nextSession;
    std::istringstream nextLoad(modified.dump());
    check(nextSession.load(nextLoad, error) && nextSession.enabled, "new session applies JSON startup ON");
}

int main(int argc, char** argv)
{
    const Rules regional = {{"wlpg", false}, {"wipg", false}, {"elpg", false}, {"eipg", false}};
    const Rules opposing = {{"OPPOSING", false}, {"unrelated", true}};
    Snapshot snapshot;
    Controller controller;
    check(argc == 2, "configuration path supplied");
    std::ifstream configFile(argv[1]);
    std::string error;
    check(controller.load(configFile, error), error.c_str());
    auto rules = regional;
    snapshot["LFPG"] = {{"26L", "27R"}, {"26R", "27L"}};
    snapshot["LFPO"] = {{"06"}, {"07"}};
    check(controller.enabled, "Auto starts enabled");
    controller.enabled = false;
    check(!controller.apply("LFPN", snapshot, rules) && rules == regional, "explicit OFF prevents automatic changes");
    controller.enabled = true;
    check(controller.apply("LFPN", snapshot, rules) && rules.at("wipg"), "user West Unlinked example");
    check(!controller.apply("LFPN", snapshot, rules), "same snapshot is idempotent");
    for (const auto& airport : {"LFPG", "LFPO", "LFPB"})
    {
        auto r = opposing;
        check(controller.apply(airport, snapshot, r) && r.at("OPPOSING") && r.at("unrelated"),
            "opposing applied without touching unrelated rules");
    }
    for (const auto& airport : {"LFPN", "LFPT", "LFPV"})
    {
        for (const auto& pg : {"26R", "08L"})
            for (const auto& po : {"25", "07"})
            {
                snapshot["LFPG"] = {{}, {pg}};
                snapshot["LFPO"] = {{po}, {}};
                const bool west = std::string(pg) == "26R";
                const bool linked = west == (std::string(po) == "25");
                const std::string expected = west ? (linked ? "wlpg" : "wipg") : (linked ? "elpg" : "eipg");
                auto r = regional;
                check(controller.apply(airport, snapshot, r) && r.at(expected), "all four Paris states");
                int count = 0;
                for (const auto& [key, value] : r) count += value;
                check(count == 1, "regional modes are exclusive");
            }
    }
    snapshot["LFPG"] = {{"26L"}, {"26R"}};
    snapshot["LFPO"] = {{"06"}, {"07"}};
    rules = regional;
    controller.apply("LFPN", snapshot, rules);
    const auto lastGood = rules;
    snapshot["LFPG"].arrivals.insert("08R");
    check(!controller.apply("LFPN", snapshot, rules) && rules == lastGood, "conflicting roles retain rules");
    check(controller.statuses().at("LFPN").find("UNDETERMINED") != std::string::npos, "conflict is visible");
    snapshot.erase("LFPG");
    check(!controller.apply("LFPN", snapshot, rules), "missing reference retains rules");
    snapshot["LFPG"] = {{}, {"26R"}};
    snapshot["LFPO"] = {{}, {"20"}};
    check(!controller.detect("LFPN", snapshot, rules).ready, "crosswind-only Orly is unknown");
    snapshot["LFPO"].arrivals.insert("06");
    check(controller.detect("LFPN", snapshot, rules).ready, "crosswind plus known Orly direction");
    snapshot["LFPG"].departures.insert("99");
    check(!controller.detect("LFPN", snapshot, rules).ready, "unexpected runway prevents guessing");
    snapshot["LFPG"] = {{}, {"09r"}};
    snapshot.erase("LFPO");
    auto beauvais = Rules{{"pgeast", false}};
    check(controller.apply("LFOB", snapshot, beauvais) && beauvais.at("pgeast"), "Beauvais only needs CDG");
    snapshot["LFPG"] = {{}, {"27L"}};
    check(controller.apply("LFOB", snapshot, beauvais) && !beauvais.at("pgeast"), "Beauvais west resets rule");
    snapshot["LFPO"] = {{}, {"25"}};
    rules = regional;
    rules["eipg"] = true;
    controller.setManual("lfpn", rules);
    check(controller.isManual("LFPN"), "case-insensitive manual override");
    auto reloaded = regional;
    check(controller.apply("LFPN", snapshot, reloaded) && reloaded == rules, "manual survives airport reload");
    controller.enabled = false;
    reloaded = regional;
    check(controller.apply("LFPN", snapshot, reloaded) && reloaded == rules, "manual preserved with global Auto off");
    controller.resume("LFPN");
    controller.enabled = true;
    check(controller.apply("LFPN", snapshot, reloaded) && reloaded.at("wlpg") && !reloaded.at("eipg"), "resume restores detection");
    const Rules incomplete{{"wlpg", true}};
    check(!controller.detect("LFPN", snapshot, incomplete).ready, "partial rule set never modified");
    check(!controller.detect("EDDF", snapshot, opposing).supported, "same rule name at unrelated airport untouched");
    check(!controller.detect("LFPM", snapshot, {}).supported, "airport with no regional rule untouched");
    controller.enabled = false;
    snapshot["LFPG"] = {{}, {"08L"}};
    check(!controller.apply("LFPN", snapshot, reloaded) && reloaded.at("wlpg"), "global off freezes automatic selection");
    testJsonConfiguration();
    std::cout << "Auto configuration tests passed\n";
}
