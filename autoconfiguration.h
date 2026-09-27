#pragma once

#include <map>
#include <set>
#include <string>
#include <istream>
#include <vector>

namespace vsid::autoconfig
{
    using Rules = std::map<std::string, bool>;
    struct Runways
    {
        std::set<std::string> arrivals;
        std::set<std::string> departures;
    };
    using Snapshot = std::map<std::string, Runways>;

    struct Decision
    {
        bool supported = false;
        bool ready = false;
        std::string status;
        Rules rules;
    };

    struct Reference
    {
        std::string source;
        std::map<std::string, std::set<std::string>> groups;
        std::set<std::string> ignore;
    };

    struct Profile
    {
        std::string name;
        std::map<std::string, std::string> when;
        std::map<std::string, Rules> apply;
    };

    class Controller
    {
    public:
        bool enabled = true;
        // Validate completely before replacing the last working configuration.
        bool load(std::istream& input, std::string& error);
        bool loaded() const { return !profiles.empty(); }
        bool observes(const std::string& icao) const;
        Decision detect(const std::string& icao, const Snapshot& runways, const Rules& rules) const;
        void setManual(const std::string& icao, const Rules& rules);
        void resume(const std::string& icao);
        bool isManual(const std::string& icao) const;
        // Overrides survive airport deactivation/re-activation during this session.
        bool apply(const std::string& icao, const Snapshot& runways, Rules& rules);
        const std::map<std::string, std::string>& statuses() const { return status; }

    private:
        std::map<std::string, Reference> references;
        std::vector<Profile> profiles;
        std::map<std::string, std::set<std::string>> managedRules;
        std::map<std::string, Rules> manual;
        std::map<std::string, std::string> status;
    };
}
