#include "sidconfigv2.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace
{
	using Json = nlohmann::ordered_json;
	using vsid::v2::AirportConfig;
	using vsid::v2::Entry;

	struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

	[[noreturn]] void fail(const std::string& message) { throw ParseError(message); }

	std::string lower(std::string_view text)
	{
		std::string result(text);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	std::vector<std::string> splitCsv(const std::string& text)
	{
		std::vector<std::string> result;
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find(',', start);
			if (end == std::string::npos) end = text.size();
			size_t b = start, e = end;
			while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
			while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
			if (e > b) result.push_back(text.substr(b, e - b));
			start = end + 1;
		}
		return result;
	}

	bool isDigits(std::string_view text)
	{
		return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
	}

	std::string stringField(const Json& object, const char* key, const std::string& where, bool required)
	{
		auto it = object.find(key);
		if (it == object.end())
		{
			if (required) fail(where + ": missing '" + key + "'");
			return "";
		}
		if (!it->is_string()) fail(where + ": '" + key + "' must be a string");
		return it->get<std::string>();
	}

	// Overlays {J,P,T} values (or one number for all three) onto the map.
	void overlayClimb(std::map<char, int>& climb, const Json& value, const std::string& where)
	{
		if (value.is_number())
		{
			for (char c : { 'J', 'P', 'T' }) climb[c] = value.get<int>();
			return;
		}
		if (!value.is_object()) fail(where + ": initialClimb must be a number or an object");
		for (const auto& [key, v] : value.items())
		{
			if (key != "J" && key != "P" && key != "T") fail(where + ": initialClimb engine must be J, P or T, got '" + key + "'");
			if (!v.is_number()) fail(where + ": initialClimb." + key + " must be a number");
			climb[key[0]] = v.get<int>();
		}
	}

	std::map<std::string, bool> readBoolMap(const Json& value, const std::string& where)
	{
		if (!value.is_object()) fail(where + " must be an object");
		std::map<std::string, bool> result;
		for (const auto& [key, v] : value.items())
		{
			if (!v.is_boolean()) fail(where + "." + key + " must be true or false");
			result[key] = v.get<bool>();
		}
		return result;
	}

	// Per-waypoint defaults, layered airport -> waypoint.
	struct Defaults
	{
		std::map<char, int> climb;
		std::map<std::string, bool> equip;
	};

	void overlayDefaults(Defaults& defaults, const Json& json, const std::string& where)
	{
		if (!json.is_object()) fail(where + " must be an object");
		if (auto it = json.find("initialClimb"); it != json.end()) overlayClimb(defaults.climb, *it, where);
		if (auto it = json.find("equip"); it != json.end()) defaults.equip = readBoolMap(*it, where + ".equip");
	}

	void readCondition(const Json& json, const std::string& where, vsid::v2::RwyCondition& out)
	{
		if (!json.is_object()) fail(where + " must be an object");
		for (const auto& [kind, body] : json.items())
		{
			if (kind != "allow" && kind != "deny") fail(where + ": unexpected '" + kind + "', expected allow or deny");
			if (!body.is_object()) fail(where + "." + kind + " must be an object");
			const std::string all = stringField(body, "all", where + "." + kind, false);
			const std::string any = stringField(body, "any", where + "." + kind, false);
			(kind == "allow" ? out.allowAll : out.denyAll) = all;
			(kind == "allow" ? out.allowAny : out.denyAny) = any;
		}
	}

	Entry readEntry(const Json& json, const std::string& waypoint, const Defaults& defaults, const std::set<std::string>& runways,
		const std::map<std::string, bool>& customRules)
	{
		const std::string where = "procedures." + waypoint;
		if (!json.is_object()) fail(where + ": entry must be an object");

		Entry entry;
		entry.waypoint = waypoint;
		entry.designator = stringField(json, "sid", where, true);
		if (entry.designator.empty()) fail(where + ": empty designator (sid)");
		entry.rwys = splitCsv(stringField(json, "rwy", where + "/" + entry.designator, true));
		if (entry.rwys.empty()) fail(where + "/" + entry.designator + ": rwy must list at least one runway");
		for (const auto& rwy : entry.rwys)
			if (!runways.empty() && !runways.contains(rwy))
				fail(where + "/" + entry.designator + ": unknown runway " + rwy + " (not in runways)");

		if (auto it = json.find("climbvia"); it != json.end())
		{
			if (!it->is_boolean()) fail(where + "/" + entry.designator + ": climbvia must be true or false");
			entry.climbvia = it->get<bool>();
		}

		entry.initialClimb = defaults.climb;
		if (auto it = json.find("initialClimb"); it != json.end()) overlayClimb(entry.initialClimb, *it, where + "/" + entry.designator);

		entry.engines = stringField(json, "engines", where + "/" + entry.designator, false);
		for (char c : entry.engines)
			if (c != 'J' && c != 'P' && c != 'T') fail(where + "/" + entry.designator + ": engines must be a subset of JPT");

		entry.equip = defaults.equip;
		if (auto it = json.find("equip"); it != json.end()) entry.equip = readBoolMap(*it, where + "/" + entry.designator + ".equip");

		entry.wtc = stringField(json, "wtc", where + "/" + entry.designator, false);
		if (auto it = json.find("dest"); it != json.end()) entry.dest = readBoolMap(*it, where + "/" + entry.designator + ".dest");
		if (auto it = json.find("actArrRwy"); it != json.end()) readCondition(*it, where + "/" + entry.designator + ".actArrRwy", entry.actArrRwy);

		if (auto it = json.find("rule"); it != json.end())
		{
			if (it->is_string()) entry.rules.push_back(it->get<std::string>());
			else if (it->is_array())
			{
				for (const auto& r : *it)
				{
					if (!r.is_string()) fail(where + "/" + entry.designator + ": rule list must hold strings");
					entry.rules.push_back(r.get<std::string>());
				}
			}
			else fail(where + "/" + entry.designator + ": rule must be a string or a list of strings");
			for (const auto& r : entry.rules)
				if (!customRules.contains(r)) fail(where + "/" + entry.designator + ": unknown rule '" + r + "' (not in customRules)");
		}
		return entry;
	}

	void requireGroup(const AirportConfig& config, const std::string& group, const std::string& where)
	{
		if (!config.runwayGroups.contains(group)) fail(where + ": unknown group '" + group + "'");
	}

	void parseInto(const Json& airport, const std::string& icao, AirportConfig& config)
	{
		if (!airport.is_object()) fail("airport must be an object");
		config.icao = icao;

		auto version = airport.find("version");
		if (version == airport.end() || !version->is_number_integer() || version->get<int>() != 2)
			fail("config version 2 required (missing or wrong \"version\")");

		if (auto it = airport.find("customRules"); it != airport.end()) config.customRules = readBoolMap(*it, "customRules");

		std::set<std::string> runways;
		if (auto it = airport.find("runways"); it != airport.end())
		{
			if (!it->is_string()) fail("runways must be a CSV string");
			for (auto& r : splitCsv(it->get<std::string>())) runways.insert(r);
		}

		if (auto it = airport.find("runwayGroups"); it != airport.end())
		{
			if (!it->is_object()) fail("runwayGroups must be an object");
			for (const auto& [name, list] : it->items())
			{
				if (!list.is_array()) fail("runwayGroups." + name + " must be a list");
				for (const auto& r : list)
				{
					if (!r.is_string()) fail("runwayGroups." + name + " must hold strings");
					config.runwayGroups[name].push_back(r.get<std::string>());
				}
			}
		}

		Defaults airportDefaults;
		for (char c : { 'J', 'P', 'T' }) airportDefaults.climb[c] = airport.value("maxInitialClimb", 0);
		if (auto it = airport.find("defaults"); it != airport.end()) overlayDefaults(airportDefaults, *it, "defaults");

		std::set<std::string> waypoints;
		if (auto it = airport.find("procedures"); it != airport.end())
		{
			if (!it->is_object()) fail("procedures must be an object");
			for (const auto& [waypoint, body] : it->items())
			{
				waypoints.insert(waypoint);
				Defaults local = airportDefaults;
				const Json* list = &body;
				if (body.is_object()) // { "defaults": {...}, "entries": [...] }
				{
					if (auto d = body.find("defaults"); d != body.end()) overlayDefaults(local, *d, "procedures." + waypoint + ".defaults");
					auto e = body.find("entries");
					if (e == body.end()) fail("procedures." + waypoint + ": missing 'entries'");
					list = &*e;
				}
				if (!list->is_array()) fail("procedures." + waypoint + " must be a list of entries");
				for (const auto& item : *list)
				{
					Entry entry = readEntry(item, waypoint, local, runways, config.customRules);
					entry.id = static_cast<int>(config.entries.size());
					config.entries.push_back(std::move(entry));
				}
			}
		}

		if (auto it = airport.find("selection"); it != airport.end())
		{
			if (!it->is_object()) fail("selection must be an object");
			for (const auto& [waypoint, list] : it->items())
			{
				if (!waypoints.contains(waypoint)) fail("selection: unknown waypoint '" + waypoint + "'");
				if (!list.is_array()) fail("selection." + waypoint + " must be a list of groups");
				for (const auto& g : list)
				{
					if (!g.is_string()) fail("selection." + waypoint + " must hold group names");
					requireGroup(config, g.get<std::string>(), "selection." + waypoint);
					config.selection[waypoint].push_back(g.get<std::string>());
				}
			}
		}

		if (auto it = airport.find("areas"); it != airport.end())
		{
			if (!it->is_object()) fail("areas must be an object");
			for (const auto& [name, body] : it->items())
			{
				const std::string where = "area '" + name + "'";
				vsid::v2::Area area;
				area.name = name;
				area.group = stringField(body, "group", where, true);
				if (area.group != "default") requireGroup(config, area.group, where);
				auto points = body.find("points");
				if (points == body.end() || !points->is_array() || points->size() < 3) fail(where + ": needs at least 3 points");
				for (const auto& p : *points)
				{
					const auto lat = p.is_object() && p.contains("lat") && p["lat"].is_string() ? vsid::v2::parseCoordinate(p["lat"].get<std::string>()) : std::nullopt;
					const auto lon = p.is_object() && p.contains("lon") && p["lon"].is_string() ? vsid::v2::parseCoordinate(p["lon"].get<std::string>()) : std::nullopt;
					if (!lat || !lon) fail(where + ": malformed coordinate");
					area.points.push_back({ *lat, *lon });
				}
				config.areas.push_back(std::move(area));
			}
		}

		if (auto it = airport.find("strategies"); it != airport.end())
		{
			if (!it->is_object()) fail("strategies must be an object");
			for (const auto& [id, body] : it->items())
			{
				if (id.empty() || id.find_first_of(" \t") != std::string::npos || lower(id) == "nominal")
					fail("strategy id '" + id + "' must have no space and not be 'nominal'");
				vsid::v2::Strategy strategy;
				strategy.id = id;
				strategy.label = stringField(body, "label", "strategy " + id, false);
				auto assign = body.find("assign");
				if (assign == body.end() || !assign->is_object()) fail("strategy " + id + ": 'assign' must be an object");
				for (const auto& [key, mode] : assign->items())
				{
					if (key != "*" && !waypoints.contains(key)) fail("strategy " + id + ": unknown waypoint '" + key + "'");
					if (!mode.is_string()) fail("strategy " + id + ": invalid mode for '" + key + "'");
					const std::string m = mode.get<std::string>();
					if (m != "nearest" && m != "default" && !config.runwayGroups.contains(m))
						fail("strategy " + id + ": invalid mode '" + m + "' for '" + key + "' (unknown group)");
					strategy.assign.emplace_back(key, m);
				}
				config.strategies.push_back(std::move(strategy));
			}
		}

		if (auto it = airport.find("defaultStrategy"); it != airport.end())
		{
			if (!it->is_string()) fail("defaultStrategy must be a string");
			config.defaultStrategy = it->get<std::string>();
			if (!config.defaultStrategy.empty() && !config.findStrategy(config.defaultStrategy))
				fail("defaultStrategy '" + config.defaultStrategy + "' is not a declared strategy");
		}
	}
}

namespace vsid::v2
{
	std::string AirportConfig::groupOf(const std::string& rwy) const
	{
		for (const auto& [name, rwys] : runwayGroups)
			if (std::find(rwys.begin(), rwys.end(), rwy) != rwys.end()) return name;
		return "";
	}

	const Strategy* AirportConfig::findStrategy(std::string_view id) const
	{
		const std::string wanted = lower(id);
		for (const auto& s : strategies)
			if (lower(s.id) == wanted) return &s;
		return nullptr;
	}

	std::optional<double> parseCoordinate(std::string_view text)
	{
		// H DDD . MM . SS . FFF
		if (text.size() < 2) return std::nullopt;
		const char hemi = text[0];
		if (hemi != 'N' && hemi != 'S' && hemi != 'E' && hemi != 'W') return std::nullopt;

		std::vector<std::string_view> parts;
		size_t start = 1;
		while (true)
		{
			const size_t dot = text.find('.', start);
			parts.push_back(text.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
			if (dot == std::string_view::npos) break;
			start = dot + 1;
		}
		if (parts.size() != 4) return std::nullopt;
		for (auto p : parts) if (!isDigits(p)) return std::nullopt;

		auto number = [](std::string_view s) { double v = 0; for (char c : s) v = v * 10 + (c - '0'); return v; };
		const double deg = number(parts[0]);
		const double min = number(parts[1]);
		double frac = number(parts[3]);
		for (size_t i = 0; i < parts[3].size(); ++i) frac /= 10;
		const double value = deg + min / 60 + (number(parts[2]) + frac) / 3600;
		return (hemi == 'S' || hemi == 'W') ? -value : value;
	}

	bool parseAirport(const nlohmann::ordered_json& airport, const std::string& icao, AirportConfig& out, std::string& error)
	{
		AirportConfig parsed;
		try
		{
			parseInto(airport, icao, parsed);
		}
		catch (const ParseError& e)
		{
			error = icao + ": " + e.what();
			return false;
		}
		catch (const nlohmann::json::exception& e)
		{
			error = icao + ": malformed value (" + e.what() + ")";
			return false;
		}
		out = std::move(parsed);
		return true;
	}
}
