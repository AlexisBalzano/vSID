#pragma once

#include "include/nlohmann/json.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vsid::v2
{
	struct GeoPoint { double lat = 0; double lon = 0; };
	struct RwyCondition { std::string allowAll, allowAny, denyAll, denyAny; }; // CSV, "" = absent
	struct Entry
	{
		int id = 0;                          // global index inside the airport, file order
		std::string waypoint, designator;
		std::vector<std::string> rwys;       // file order kept
		bool climbvia = false;
		std::map<char, int> initialClimb;    // resolved: keys 'J', 'P', 'T' always present
		std::string engines;                 // "" = all, else a subset of "JPT"
		std::map<std::string, bool> equip;   // resolved with defaults
		std::string wtc;                     // "" = all
		std::map<std::string, bool> dest;
		RwyCondition actArrRwy;
		std::vector<std::string> rules;      // empty = no rule; else "at least one active"
	};
	struct Area { std::string name; std::string group; std::vector<GeoPoint> points; }; // group = group name or "default"
	struct Strategy { std::string id; std::string label; std::vector<std::pair<std::string, std::string>> assign; }; // key = waypoint or "*"
	struct AirportConfig
	{
		std::string icao;
		std::map<std::string, std::vector<std::string>> runwayGroups;
		std::vector<Entry> entries;                                   // all waypoints, file order
		std::map<std::string, std::vector<std::string>> selection;    // waypoint -> group order
		std::vector<Area> areas;                                      // declaration order
		std::vector<Strategy> strategies;                             // declaration order
		std::string defaultStrategy;                                  // "" = nominal
		std::map<std::string, bool> customRules;                      // initial values from the file
		std::string groupOf(const std::string& rwy) const;            // "" if in no group
		const Strategy* findStrategy(std::string_view id) const;      // case insensitive
	};

	// "N049.00.26.119" -> decimal degrees, S/W negative; nullopt if malformed
	std::optional<double> parseCoordinate(std::string_view text);
	// Parses one airport object of a v2 file. On refusal returns false with a message in error.
	bool parseAirport(const nlohmann::ordered_json& airport, const std::string& icao, AirportConfig& out, std::string& error);
}
