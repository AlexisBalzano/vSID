#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

// vSMR's existing Paris bridge wire contract. This describes actual runtime
// rules, not runway detection policy (which remains in vSidAutoConfig.json).
namespace vsid::bridgeconfig
{
    inline constexpr std::array<std::string_view, 6> Airports = {
        "LFPG", "LFPO", "LFPN", "LFPV", "LFPT", "LFOB"
    };
    inline constexpr std::array<std::string_view, 4> RegionalRules = {"WLPG", "ELPG", "WIPG", "EIPG"};

    template<class Rules>
    std::string paris(std::string_view airport, const Rules& rules, bool automatic)
    {
        char flow = '?', linked = '?';
        if (airport == "LFPG" || airport == "LFPO")
        {
            // OPPOSING drives the SID rules; legacy LINKED aliases can be stale.
            const auto opposing = rules.find("OPPOSING");
            if (opposing != rules.end()) linked = opposing->second ? 'U' : 'L';
        }
        else
        {
            std::string_view selected;
            for (const auto key : RegionalRules)
            {
                const auto rule = rules.find(std::string(key));
                if (rule == rules.end() || !rule->second) continue;
                if (!selected.empty()) return std::string(airport) + "=??" + (automatic ? "A;" : "M;");
                selected = key;
            }
            if (!selected.empty())
            {
                flow = selected.front();
                linked = selected[1] == 'L' ? 'L' : 'U';
            }
        }
        return std::string(airport) + '=' + flow + linked + (automatic ? "A;" : "M;");
    }

    inline std::string taxi(std::optional<bool> north, std::optional<bool> south, bool otherActive = false)
    {
        if (!north || !south || *north != *south || otherActive) return "?";
        return *north ? "M" : "G";
    }

    // Validate before changing anything; never create missing airport rules.
    template<class Rules>
    bool select(Rules& rules, std::string_view airport, std::string_view selection)
    {
        if (airport == "LFPG" || airport == "LFPO")
        {
            if ((selection != "LINKED" && selection != "UNLINKED") || !rules.contains("OPPOSING")) return false;
            rules.at("OPPOSING") = selection == "UNLINKED";
            return true;
        }
        if (airport != "LFPN" && airport != "LFPT" && airport != "LFPV" && airport != "LFOB") return false;
        bool recognized = false;
        for (const auto key : RegionalRules)
        {
            if (!rules.contains(std::string(key))) return false;
            recognized = recognized || key == selection;
        }
        if (!recognized) return false;
        for (const auto key : RegionalRules) rules.at(std::string(key)) = key == selection;
        if (auto east = rules.find("PGEAST"); east != rules.end()) east->second = selection.front() == 'E';
        return true;
    }
}
