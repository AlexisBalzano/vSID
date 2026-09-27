#include "autoconfiguration.h"
#include "include/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vsid::autoconfig
{
    namespace
    {
        using Json = nlohmann::json;

        std::string upper(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            return text;
        }

        void require(bool valid, const std::string& message)
        {
            if (!valid) throw std::runtime_error(message);
        }

        std::string identifier(const std::string& value)
        {
            require(!value.empty() && std::none_of(value.begin(), value.end(),
                [](unsigned char c) { return std::isspace(c); }), "Empty identifier or whitespace in: " + value);
            return upper(value);
        }

        void fields(const Json& object, const std::set<std::string>& allowed, const std::string& context)
        {
            require(object.is_object(), context + " must be an object");
            for (const auto& [key, value] : object.items())
                require(allowed.contains(key), context + ": unknown field " + key);
        }

        std::set<std::string> runways(const Json& array)
        {
            require(array.is_array(), "Runways must be an array of strings");
            std::set<std::string> result;
            for (const auto& item : array)
            {
                require(item.is_string(), "Runway must be a string (e.g. \"07\")");
                const auto rwy = identifier(item.get<std::string>());
                require((rwy.size() == 2 || (rwy.size() == 3 && std::string("LCR").find(rwy[2]) != std::string::npos)) &&
                    rwy[0] >= '0' && rwy[0] <= '3' && rwy[1] >= '0' && rwy[1] <= '9' &&
                    rwy.substr(0, 2) >= "01" && rwy.substr(0, 2) <= "36", "Invalid runway: " + rwy);
                require(result.insert(rwy).second, "Duplicate runway: " + rwy);
            }
            return result;
        }

        // Empty means unavailable, unknown runway, or conflicting group selections.
        std::string group(const Reference& reference, const Runways& selected)
        {
            std::string result;
            for (const auto* role : { &selected.arrivals, &selected.departures })
            {
                if (role == &selected.arrivals && reference.source == "departures") continue;
                if (role == &selected.departures && reference.source == "arrivals") continue;
                for (const auto& raw : *role)
                {
                    const auto rwy = upper(raw);
                    if (reference.ignore.contains(rwy)) continue;
                    auto found = std::find_if(reference.groups.begin(), reference.groups.end(),
                        [&](const auto& item) { return item.second.contains(rwy); });
                    if (found == reference.groups.end()) return {};
                    if (!result.empty() && result != found->first) return {};
                    result = found->first;
                }
            }
            return result;
        }
    }

    bool Controller::load(std::istream& input, std::string& error)
    {
        try
        {
            require(static_cast<bool>(input), "Cannot read Auto configuration JSON");
            // Reject duplicate keys rather than silently using the last occurrence.
            std::vector<std::set<std::string>> objectKeys;
            const auto document = Json::parse(input, [&](int, Json::parse_event_t event, Json& value) {
                if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
                else if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
                else if (event == Json::parse_event_t::key)
                    require(objectKeys.back().insert(value.get<std::string>()).second,
                        "Duplicate JSON key: " + value.get<std::string>());
                return true;
            });
            fields(document, { "version", "enabledByDefault", "references", "profiles" }, "Root");
            require(!document.contains("enabledByDefault") || document.at("enabledByDefault").is_boolean(),
                "enabledByDefault must be true or false");
            const bool startupEnabled = document.value("enabledByDefault", true);
            require(document.at("version").is_number_integer() && document.at("version") == 1,
                "Unsupported Auto configuration version; expected 1");
            const auto& sourceReferences = document.at("references");
            require(sourceReferences.is_object() && !sourceReferences.empty(), "references must be a nonempty object");
            std::map<std::string, Reference> newReferences;
            for (const auto& [rawIcao, definition] : sourceReferences.items())
            {
                const auto icao = identifier(rawIcao);
                fields(definition, { "source", "groups", "ignore" }, "Reference " + icao);
                Reference reference;
                reference.source = definition.at("source").get<std::string>();
                require(reference.source == "both" || reference.source == "arrivals" || reference.source == "departures",
                    icao + ": source must be both, arrivals, or departures");
                reference.ignore = runways(definition.value("ignore", Json::array()));
                const auto& groups = definition.at("groups");
                require(groups.is_object() && !groups.empty(), icao + ": groups must be a nonempty object");
                auto assigned = reference.ignore;
                for (const auto& [rawName, values] : groups.items())
                {
                    const auto name = identifier(rawName);
                    auto members = runways(values);
                    require(!members.empty(), icao + ": empty group " + name);
                    for (const auto& rwy : members)
                        require(assigned.insert(rwy).second, icao + ": runway belongs to multiple groups or ignore: " + rwy);
                    require(reference.groups.emplace(name, std::move(members)).second, icao + ": duplicate group " + name);
                }
                require(newReferences.emplace(icao, std::move(reference)).second, "Duplicate reference: " + icao);
            }

            const auto& sourceProfiles = document.at("profiles");
            require(sourceProfiles.is_array() && !sourceProfiles.empty(), "profiles must be a nonempty array");
            std::vector<Profile> newProfiles;
            std::set<std::string> names;
            std::map<std::string, std::set<std::string>> newManagedRules;
            for (const auto& definition : sourceProfiles)
            {
                fields(definition, { "name", "when", "apply" }, "Profile");
                Profile profile;
                profile.name = definition.at("name").get<std::string>();
                require(!profile.name.empty() && names.insert(upper(profile.name)).second, "Empty or duplicate profile name: " + profile.name);
                const auto& conditions = definition.at("when");
                require(conditions.is_object() && !conditions.empty(), profile.name + ": when must be a nonempty object");
                for (const auto& [rawIcao, rawGroup] : conditions.items())
                {
                    const auto icao = identifier(rawIcao);
                    const auto name = identifier(rawGroup.get<std::string>());
                    require(newReferences.contains(icao) && newReferences.at(icao).groups.contains(name),
                        profile.name + ": unknown reference/group " + icao + "/" + name);
                    require(profile.when.emplace(icao, name).second, profile.name + ": duplicate condition " + icao);
                }
                const auto& targets = definition.at("apply");
                require(targets.is_object() && !targets.empty(), profile.name + ": apply must be a nonempty object");
                for (const auto& [rawIcao, values] : targets.items())
                {
                    const auto icao = identifier(rawIcao);
                    require(values.is_object() && !values.empty(), profile.name + ": target rules must be a nonempty object");
                    Rules target;
                    std::set<std::string> keys;
                    for (const auto& [rawKey, value] : values.items())
                    {
                        const auto key = identifier(rawKey);
                        require(value.is_boolean(), profile.name + ": rule " + key + " must be true or false");
                        require(target.emplace(key, value.get<bool>()).second, profile.name + ": duplicate rule " + key);
                        keys.insert(key);
                    }
                    // Alternatives explicitly reset all managed rules, preventing stale flags.
                    if (newManagedRules.contains(icao))
                        require(newManagedRules.at(icao) == keys, profile.name + ": inconsistent rule keys for " + icao);
                    else newManagedRules.emplace(icao, std::move(keys));
                    require(profile.apply.emplace(icao, std::move(target)).second, profile.name + ": duplicate target " + icao);
                }
                newProfiles.push_back(std::move(profile));
            }
            // Apply the startup preference once; reload must preserve a user's ON/OFF command.
            if (!loaded()) enabled = startupEnabled;
            references = std::move(newReferences);
            profiles = std::move(newProfiles);
            managedRules = std::move(newManagedRules);
            status.clear();
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    bool Controller::observes(const std::string& icao) const
    {
        return references.contains(upper(icao));
    }

    Decision Controller::detect(const std::string& airport, const Snapshot& snapshot, const Rules& rules) const
    {
        const auto icao = upper(airport);
        Decision result;
        if (!managedRules.contains(icao))
        {
            result.status = loaded() ? "No runway mapping in Auto configuration JSON" : "Auto configuration JSON not loaded";
            return result;
        }
        std::map<std::string, std::string> keys;
        for (const auto& [key, value] : rules) keys[upper(key)] = key;
        for (const auto& key : managedRules.at(icao))
            if (!keys.contains(key))
            {
                result.status = "Existing airport config is missing rule " + key + "; rules retained";
                return result;
            }
        result.supported = true;
        const Profile* match = nullptr;
        bool uncertain = false;
        for (const auto& profile : profiles)
        {
            if (!profile.apply.contains(icao)) continue;
            bool mismatch = false, unknown = false;
            for (const auto& [reference, expected] : profile.when)
            {
                const auto it = snapshot.find(reference);
                const auto actual = it == snapshot.end() ? std::string{} : group(references.at(reference), it->second);
                if (actual.empty()) unknown = true;
                else if (actual != expected) mismatch = true;
            }
            if (mismatch) continue;
            if (unknown) { uncertain = true; continue; }
            if (match)
            {
                result.status = "AMBIGUOUS: multiple Auto profiles match; rules retained";
                return result;
            }
            match = &profile;
        }
        if (uncertain || !match)
        {
            result.status = "UNDETERMINED: missing/conflicting runway data or no matching Auto profile; rules retained";
            return result;
        }
        result.ready = true;
        result.status = match->name;
        for (const auto& [key, value] : match->apply.at(icao)) result.rules[keys.at(key)] = value;
        return result;
    }

    void Controller::setManual(const std::string& icao, const Rules& rules)
    {
        manual[upper(icao)] = rules;
        status[upper(icao)] = "MANUAL override";
    }

    void Controller::resume(const std::string& icao) { manual.erase(upper(icao)); }
    bool Controller::isManual(const std::string& icao) const { return manual.contains(upper(icao)); }

    bool Controller::apply(const std::string& airport, const Snapshot& snapshot, Rules& rules)
    {
        const auto icao = upper(airport);
        const auto before = rules;
        if (const auto it = manual.find(icao); it != manual.end())
        {
            for (auto& [key, value] : rules)
                for (const auto& [savedKey, savedValue] : it->second)
                    if (upper(key) == upper(savedKey)) value = savedValue;
            status[icao] = "MANUAL override";
        }
        else if (!enabled) status[icao] = "OFF";
        else
        {
            const auto decision = detect(icao, snapshot, rules);
            status[icao] = decision.status;
            if (decision.ready)
                for (const auto& [key, value] : decision.rules) rules.at(key) = value;
        }
        return before != rules;
    }
}
