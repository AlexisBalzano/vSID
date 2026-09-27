#include "../bridgeconfiguration.h"
#include <cstdlib>
#include <iostream>
#include <map>

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    using namespace vsid::bridgeconfig;
    using Rules = std::map<std::string, bool>;
    Rules pg{{"OPPOSING", true}, {"LINKED", true}, {"UNLINKED", false}};
    check(paris("LFPG", pg, true) == "LFPG=?UA;", "Auto uses effective opposing rule, not stale alias");
    check(select(pg, "LFPG", "LINKED") && paris("LFPG", pg, false) == "LFPG=?LM;", "manual explicit linked selection");
    Rules regional{{"WLPG", false}, {"ELPG", false}, {"WIPG", true}, {"EIPG", false}};
    check(paris("LFPN", regional, true) == "LFPN=WUA;", "Auto WIPG reaches existing vSMR wire contract");
    for (const auto name : Airports)
        check(paris(name, Rules{}, false).size() == 9, "fixed record width");
    regional["ELPG"] = true;
    check(paris("LFPN", regional, true) == "LFPN=??A;", "ambiguous runtime rules publish unknown");
    check(select(regional, "LFPN", "ELPG") && paris("LFPN", regional, false) == "LFPN=ELM;", "regional selection exclusive");
    const auto before = regional;
    check(!select(regional, "LFPN", "INVALID") && before == regional, "invalid selection atomic");
    Rules incomplete{{"WLPG", true}};
    check(!select(incomplete, "LFPN", "WIPG") && incomplete == Rules{{"WLPG", true}}, "missing rules never created");
    check(paris("LFOB", Rules{{"PGEAST", true}}, true) == "LFOB=??A;", "do not infer linked state from PG direction alone");
    check(taxi(true, true) == "M" && taxi(false, false) == "G", "actual area state determines taxi mode");
    check(taxi(true, false) == "?" && taxi({}, true) == "?" && taxi(false, false, true) == "?", "mixed missing or additional areas unknown");
    std::cout << "Bridge configuration tests passed\n";
}
