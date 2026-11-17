#pragma once

#include "sidconfigv2.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vsid::v2
{
	struct Request
	{
		std::string waypoint;
		std::set<std::string> depRwys, arrRwys;
		std::string atcRwy;                       // "" = none
		char engine = 'J';                        // ES engine type; '?' unknown
		char aircraftKind = 'L';                  // ES aircraft type (L, H, ...); '?' unknown
		bool equipCheck = true;
		std::string equipment, pbn;               // as fplnhelper::getEquip / getPbn
		std::string wtc;                          // "L", "M", "H", "J" or ""
		std::string dest;
		std::optional<GeoPoint> position;
		std::map<std::string, bool> rules;        // current state of the customRules
		std::string mode = "default";             // effective strategy mode of the waypoint
		std::set<int> available;                  // entry ids present in the sector file
	};

	struct Result
	{
		std::optional<int> entryId;               // nullopt = no SID
		std::string rwy;
		int initialClimb = 0;
		bool climbvia = false;
		bool equipFailure = false;                // "EQUIP": an entry was only dropped because of equipment
	};

	// ray casting, same algorithm as the former v1 vsid::Area::inside
	bool insidePolygon(const std::vector<GeoPoint>& polygon, const GeoPoint& p);
	// "default" wins, otherwise the group of the first declared area containing p
	std::optional<std::string> areaGroupAt(const AirportConfig& cfg, const GeoPoint& p);
	Result select(const AirportConfig& cfg, const Request& req);
}
