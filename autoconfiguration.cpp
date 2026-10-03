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
				[](unsigned char c) { return std::isspace(c) || std::iscntrl(c); }), "Empty identifier or whitespace in: " + value);
			return upper(value);
		}

		std::string airportId(const std::string& value)
		{
			const auto result = identifier(value);
			require(result.size() == 4 && std::all_of(result.begin(), result.end(),
				[](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }), "Invalid airport: " + value);
			return result;
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

		struct Group
		{
			std::string name;
			bool conflict = false;
		};

		Group group(const Reference& reference, const Runways& selected)
		{
			Group result;
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
					if (found == reference.groups.end()) return { {}, true };
					if (!result.name.empty() && result.name != found->first) return { {}, true };
					result.name = found->first;
				}
			}
			return result;
		}
	}

	const char* statusName(Status status)
	{
		switch (status)
		{
		case Status::NotLoaded: return "NOT_LOADED";
		case Status::Unmanaged: return "UNMANAGED";
		case Status::MissingRule: return "MISSING_RULE";
		case Status::Off: return "OFF";
		case Status::Manual: return "MANUAL";
		case Status::Matched: return "MATCHED";
		case Status::Undetermined: return "UNDETERMINED";
		case Status::Ambiguous: return "AMBIGUOUS";
		}
		return "NOT_LOADED";
	}

	std::string describe(const State& state)
	{
		std::string text = statusName(state.code);
		if (!state.profile.empty()) text += ": " + state.profile;
		if (!state.detail.empty()) text += ": " + state.detail;
		return text;
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
			fields(document, { "version", "enabledByDefault", "references", "airportGroups", "profiles" }, "Root");
			require(!document.contains("enabledByDefault") || document.at("enabledByDefault").is_boolean(),
				"enabledByDefault must be true or false");
			const bool startupEnabled = document.value("enabledByDefault", true);
			require(document.at("version").is_number_integer() && document.at("version") == 2,
				"Unsupported Auto configuration version; expected 2 (enable arrays)");
			const auto& sourceReferences = document.at("references");
			require(sourceReferences.is_object() && !sourceReferences.empty(), "references must be a nonempty object");
			std::map<std::string, Reference> newReferences;
			for (const auto& [rawIcao, definition] : sourceReferences.items())
			{
				const auto icao = airportId(rawIcao);
				fields(definition, { "source", "groups", "ignore" }, "Reference " + icao);
				Reference reference;
				reference.source = definition.value("source", std::string("both"));
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

			std::map<std::string, std::set<std::string>> airportGroups;
			const auto sourceGroups = document.value("airportGroups", Json::object());
			require(sourceGroups.is_object(), "airportGroups must be an object");
			for (const auto& [rawName, members] : sourceGroups.items())
			{
				const auto name = identifier(rawName);
				require(name.size() != 4 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos,
					"Airport group name must not look like an airport: " + name);
				require(members.is_array() && !members.empty(), name + ": group must be a nonempty airport array");
				std::set<std::string> airports;
				for (const auto& member : members)
					require(airports.insert(airportId(member.get<std::string>())).second, name + ": duplicate airport");
				require(airportGroups.emplace(name, std::move(airports)).second, "Duplicate airport group: " + name);
			}

			// First pass: expand targets and collect each airport's managed-key union.
			const auto& sourceProfiles = document.at("profiles");
			require(sourceProfiles.is_array() && !sourceProfiles.empty(), "profiles must be a nonempty array");
			std::vector<Profile> newProfiles;
			std::set<std::string> names;
			std::map<std::string, std::set<std::string>> newManagedRules;
			for (const auto& definition : sourceProfiles)
			{
				fields(definition, { "name", "when", "enable", "fallback" }, "Profile");
				Profile profile;
				require(!definition.contains("fallback") || definition.at("fallback").is_boolean(), "fallback must be boolean");
				profile.fallback = definition.value("fallback", false);
				profile.name = definition.at("name").get<std::string>();
				require(!profile.name.empty() && names.insert(upper(profile.name)).second, "Empty or duplicate profile name: " + profile.name);
				const auto& conditions = definition.at("when");
				require(conditions.is_object() && !conditions.empty(), profile.name + ": when must be a nonempty object");
				for (const auto& [rawIcao, rawGroup] : conditions.items())
				{
					const auto icao = airportId(rawIcao);
					const auto name = identifier(rawGroup.get<std::string>());
					require(newReferences.contains(icao) && newReferences.at(icao).groups.contains(name),
						profile.name + ": unknown reference/group " + icao + "/" + name);
					require(profile.when.emplace(icao, name).second, profile.name + ": duplicate condition " + icao);
				}
				const auto& targets = definition.at("enable");
				require(targets.is_object() && !targets.empty(), profile.name + ": enable must be a nonempty object");
				for (const auto& [rawTarget, values] : targets.items())
				{
					const auto target = identifier(rawTarget);
					const auto airports = airportGroups.contains(target) ? airportGroups.at(target) : std::set<std::string>{ airportId(target) };
					require(values.is_array(), profile.name + ": enabled rules must be an array");
					Rules enabledRules;
					for (const auto& value : values)
						require(enabledRules.emplace(identifier(value.get<std::string>()), true).second, profile.name + ": duplicate rule");
					for (const auto& icao : airports)
					{
						require(profile.apply.emplace(icao, enabledRules).second, profile.name + ": overlapping targets for " + icao);
						for (const auto& [key, enabled] : enabledRules) newManagedRules[icao].insert(key);
						newManagedRules.try_emplace(icao);
					}
				}
				newProfiles.push_back(std::move(profile));
			}
			// Second pass: omitted keys are OFF; an omitted airport is untouched.
			for (auto& profile : newProfiles)
				for (auto& [icao, rules] : profile.apply)
					for (const auto& key : newManagedRules.at(icao)) rules.try_emplace(key, false);

			// A reload must never restore keys that are no longer managed.
			auto newManual = this->manual;
			for (auto& [icao, rules] : newManual)
				std::erase_if(rules, [&](const auto& rule)
				{
					return !newManagedRules.contains(icao) || !newManagedRules.at(icao).contains(rule.first);
				});
			std::erase_if(newManual, [](const auto& entry) { return entry.second.empty(); });
			this->manual = std::move(newManual);
			// Apply the startup preference once; reload must preserve a user's ON/OFF command.
			if (!this->loaded()) this->enabled = startupEnabled;
			this->references = std::move(newReferences);
			this->profiles = std::move(newProfiles);
			this->managedRules = std::move(newManagedRules);
			this->status.clear();
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
		return this->references.contains(upper(icao));
	}

	bool Controller::manages(const std::string& icao, const std::string& key) const
	{
		const auto found = this->managedRules.find(upper(icao));
		return found != this->managedRules.end() && found->second.contains(upper(key));
	}

	Decision Controller::detect(const std::string& airport, const Snapshot& snapshot, const Rules& rules) const
	{
		const auto icao = upper(airport);
		Decision result;
		if (!this->managedRules.contains(icao))
		{
			result.state.code = this->loaded() ? Status::Unmanaged : Status::NotLoaded;
			return result;
		}
		std::map<std::string, std::string> keys;
		for (const auto& [key, value] : rules) keys[upper(key)] = key;
		for (const auto& key : this->managedRules.at(icao))
			if (!keys.contains(key))
			{
				result.state = { Status::MissingRule, {}, "Existing airport config is missing rule " + key + "; rules retained" };
				return result;
			}
		result.supported = true;
		std::vector<const Profile*> matches, fallbacks;
		bool uncertain = false, uncertainFallback = false, conflict = false;
		for (const auto& profile : this->profiles)
		{
			if (!profile.apply.contains(icao)) continue;
			bool mismatch = false, unknown = false, invalid = false;
			for (const auto& [reference, expected] : profile.when)
			{
				const auto it = snapshot.find(reference);
				const auto actual = it == snapshot.end() ? Group{} : group(this->references.at(reference), it->second);
				if (actual.name.empty()) unknown = true;
				else if (actual.name != expected) mismatch = true;
				invalid = invalid || actual.conflict;
			}
			if (mismatch) continue;
			conflict = conflict || invalid;
			if (unknown)
			{
				if (!profile.fallback) uncertain = true;
				else uncertainFallback = true;
				continue;
			}
			(profile.fallback ? fallbacks : matches).push_back(&profile);
		}
		if (matches.size() > 1 || (matches.empty() && fallbacks.size() > 1))
		{
			result.state.code = Status::Ambiguous;
			return result;
		}
		// An explicit fallback permits missing selections, never conflicting runways.
		const Profile* match = !matches.empty() && !uncertain ? matches.front() :
			(matches.empty() && !fallbacks.empty() && !uncertainFallback && !conflict ? fallbacks.front() : nullptr);
		if (!match)
		{
			result.state.code = Status::Undetermined;
			return result;
		}
		result.ready = true;
		result.state = { Status::Matched, match->name, {} };
		for (const auto& [key, value] : match->apply.at(icao)) result.rules[keys.at(key)] = value;
		return result;
	}

	bool Controller::setManual(const std::string& airport, const Rules& rules)
	{
		const auto icao = upper(airport);
		const auto keys = this->managedRules.find(icao);
		if (keys == this->managedRules.end() || keys->second.empty()) return false;
		Rules saved;
		for (const auto& [key, value] : rules)
			if (keys->second.contains(upper(key))) saved[upper(key)] = value;
		if (saved.size() != keys->second.size()) return false;
		this->manual[icao] = std::move(saved);
		this->status[icao] = { this->enabled ? Status::Manual : Status::Off, {}, {} };
		return true;
	}

	bool Controller::rememberRuleChange(const std::string& icao, const std::string& key, const Rules& rules)
	{
		if (!this->manages(icao, key) || (!this->enabled && !this->isManual(icao))) return false;
		return this->setManual(icao, rules);
	}

	void Controller::resume(const std::string& icao)
	{
		this->manual.erase(upper(icao));
	}

	void Controller::resumeAll()
	{
		this->manual.clear();
	}

	bool Controller::isManual(const std::string& icao) const
	{
		return this->manual.contains(upper(icao));
	}

	std::vector<std::string> Controller::manualAirports() const
	{
		std::vector<std::string> airports;
		for (const auto& [icao, rules] : this->manual) airports.push_back(icao);
		return airports;
	}

	bool Controller::apply(const std::string& airport, const Snapshot& snapshot, Rules& rules)
	{
		const auto icao = upper(airport);
		const auto before = rules;
		if (!this->enabled) this->status[icao] = { Status::Off, {}, {} };
		else if (const auto it = this->manual.find(icao); it != this->manual.end())
		{
			for (auto& [key, value] : rules)
				if (it->second.contains(upper(key))) value = it->second.at(upper(key));
			this->status[icao] = { Status::Manual, {}, {} };
		}
		else
		{
			const auto decision = this->detect(icao, snapshot, rules);
			this->status[icao] = decision.state;
			if (decision.ready)
				for (const auto& [key, value] : decision.rules) rules.at(key) = value;
		}
		return before != rules;
	}

	State Controller::stateFor(const std::string& icao) const
	{
		const auto it = this->status.find(upper(icao));
		return it != this->status.end() ? it->second : State{};
	}

	std::vector<std::string> Controller::missingRunways(const RunwayInventory& inventory) const
	{
		std::vector<std::string> result;
		for (const auto& [icao, reference] : this->references)
			for (const auto& [name, runways] : reference.groups)
				for (const auto& runway : runways)
					if (!inventory.contains(icao) || !inventory.at(icao).contains(runway)) result.push_back(icao + "/" + runway);
		return result;
	}
}
