#pragma once

#include "../include/nlohmann/json.hpp"

namespace vsid::v2::fixtures
{
	// Strategy assignments only name the points kept in this reduced fixture.
	// Reduced LFPG config v2 shared by the engine tests. Areas "north" and "south" use real
	// coordinates of the v1 lfpg.json; "t2-mid" is a default-group triangle inside "south".
	inline nlohmann::ordered_json lfpg()
	{
		return nlohmann::ordered_json::parse(R"({
			"version": 2,
			"elevation": 392,
			"runways": "08R,08L,09R,09L,26L,26R,27L,27R",
			"transAlt": 5000,
			"maxInitialClimb": 12000,
			"customRules": { "opposing": false },
			"runwayGroups": {
				"north": ["09L", "09R", "27L", "27R"],
				"south": ["08L", "08R", "26L", "26R"]
			},
			"defaults": {
				"equip": { "RNAV": true },
				"initialClimb": { "J": 10000, "P": 5000, "T": 5000 }
			},
			"areas": {
				"north": { "group": "north", "points": [
					{ "lat": "N049.00.43.530", "lon": "E002.35.01.485" },
					{ "lat": "N049.00.24.884", "lon": "E002.29.34.870" },
					{ "lat": "N049.01.06.346", "lon": "E002.29.29.159" },
					{ "lat": "N049.01.20.351", "lon": "E002.34.55.149" } ] },
				"south": { "group": "south", "points": [
					{ "lat": "N049.00.26.119", "lon": "E002.31.27.453" },
					{ "lat": "N049.00.44.510", "lon": "E002.37.06.250" },
					{ "lat": "N048.59.23.638", "lon": "E002.37.16.331" },
					{ "lat": "N048.59.09.525", "lon": "E002.31.37.686" } ] },
				"t2-mid": { "group": "default", "points": [
					{ "lat": "N049.00.00.000", "lon": "E002.33.00.000" },
					{ "lat": "N049.00.00.000", "lon": "E002.36.00.000" },
					{ "lat": "N048.59.30.000", "lon": "E002.34.30.000" } ] }
			},
			"procedures": {
				"DIKOL": [
					{ "sid": "A", "rwy": "27L,27R" },
					{ "sid": "D", "rwy": "27L,27R", "rule": "opposing" },
					{ "sid": "B", "rwy": "26R,26L", "climbvia": true },
					{ "sid": "E", "rwy": "26R,26L", "climbvia": true, "rule": "opposing" }
				],
				"OKASI": [
					{ "sid": "A", "rwy": "27L,27R", "engines": "J", "initialClimb": { "J": 12000 } }
				],
				"LANVI": [
					{ "sid": "A", "rwy": "26R,26L" },
					{ "sid": "B", "rwy": "27L,27R" }
				],
				"PO": [
					{ "sid": "A", "rwy": "26R,26L" }
				]
			},
			"selection": {
				"DIKOL": ["north", "south"],
				"LANVI": ["south", "north"],
				"PO": ["south"]
			},
			"defaultStrategy": "roulage-mini",
			"strategies": {
				"ranux-dikol-sud": { "label": "RANUX DIKOL -> Sud", "assign": { "DIKOL": "south" } },
				"lanvi-bubli-baxir-nord": { "label": "LANVI BUBLI BAXIR -> Nord", "assign": { "LANVI": "north" } },
				"roulage-mini": { "label": "Roulage mini", "assign": { "*": "nearest" } }
			}
		})");
	}
}
