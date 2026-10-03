#pragma once

#include <istream>
#include <map>
#include <set>
#include <string>
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
	using RunwayInventory = std::map<std::string, std::set<std::string>>;

	enum class Status { NotLoaded, Unmanaged, MissingRule, Off, Manual, Matched, Undetermined, Ambiguous };
	struct State
	{
		Status code = Status::NotLoaded;
		std::string profile;
		std::string detail;
		bool operator==(const State&) const = default;
	};
	const char* statusName(Status status);
	std::string describe(const State& state);

	struct Decision
	{
		bool supported = false;
		bool ready = false;
		State state;
		Rules rules;
	};
	struct Reference
	{
		std::string source = "both";
		std::map<std::string, std::set<std::string>> groups;
		std::set<std::string> ignore;
	};
	struct Profile
	{
		std::string name;
		std::map<std::string, std::string> when;
		std::map<std::string, Rules> apply;
		bool fallback = false;
	};

	/** @brief SDK-independent matching of runway profiles and managed-rule overrides. */
	class Controller
	{
	public:
		bool enabled = true;
		/** @brief Validate version 2 completely before replacing the active mappings. */
		bool load(std::istream& input, std::string& error);
		bool loaded() const { return !this->profiles.empty(); }
		bool observes(const std::string& icao) const;
		bool manages(const std::string& icao, const std::string& key) const;
		Decision detect(const std::string& icao, const Snapshot& runways, const Rules& rules) const;
		/** @brief Explicit manual command: capture only managed keys. */
		bool setManual(const std::string& icao, const Rules& rules);
		/** @brief Unmanaged toggles and toggles while OFF never create overrides. */
		bool rememberRuleChange(const std::string& icao, const std::string& key, const Rules& rules);
		void resume(const std::string& icao);
		void resumeAll();
		bool isManual(const std::string& icao) const;
		std::vector<std::string> manualAirports() const;
		/** @brief Apply only managed keys; overrides survive airport reactivation. */
		bool apply(const std::string& icao, const Snapshot& runways, Rules& rules);
		const std::map<std::string, State>& statuses() const { return this->status; }
		std::vector<std::string> missingRunways(const RunwayInventory& inventory) const;

	private:
		std::map<std::string, Reference> references;
		std::vector<Profile> profiles;
		std::map<std::string, std::set<std::string>> managedRules;
		std::map<std::string, Rules> manual;
		std::map<std::string, State> status;
	};
}
