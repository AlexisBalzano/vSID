#include "strategy.h"
#include <algorithm>
#include <cctype>

namespace vsid::v2
{
	namespace
	{
		std::string upper(std::string_view text)
		{
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return out;
		}

		std::string lower(std::string_view text)
		{
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool hasWaypoint(const AirportConfig& cfg, const std::string& waypoint)
		{
			return std::any_of(cfg.entries.begin(), cfg.entries.end(), [&](const Entry& e) { return e.waypoint == waypoint; });
		}

		// Canonical mode ("nearest", "default" or the group name) or "" when invalid.
		std::string canonicalMode(const AirportConfig& cfg, std::string_view mode)
		{
			const std::string low = lower(mode);
			if (low == "nearest" || low == "default") return low;
			for (const auto& group : cfg.runwayGroups)
				if (lower(group.first) == low) return group.first;
			return "";
		}

		// Validates one waypoint/mode pair; fills the canonical forms.
		bool checkPair(const AirportConfig& cfg, std::string_view waypoint, std::string_view mode,
		               std::string& wpt, std::string& canon, std::string& error)
		{
			wpt = upper(waypoint);
			if (!hasWaypoint(cfg, wpt)) { error = "Unknown waypoint '" + wpt + "'"; return false; }
			canon = canonicalMode(cfg, mode);
			if (canon.empty()) { error = "Invalid mode '" + std::string(mode) + "' for " + wpt; return false; }
			return true;
		}

		bool isNominal(std::string_view id) { return lower(id) == "nominal"; }

		// Resolves an id (or "nominal") to the canonical id; "" for nominal. False when unknown.
		bool resolveStrategy(const AirportConfig& cfg, std::string_view id, std::string& canon, std::string& error)
		{
			if (isNominal(id)) { canon.clear(); return true; }
			const Strategy* strategy = cfg.findStrategy(id);
			if (!strategy) { error = "Unknown strategy '" + std::string(id) + "'"; return false; }
			canon = strategy->id;
			return true;
		}
	}

	void StrategyBook::reset(const AirportConfig& cfg)
	{
		m_active = cfg.defaultStrategy;
		m_assign.clear();
		m_source = StrategySource::Manual;
		m_manualOverride = false;
	}

	bool StrategyBook::setStrategy(const AirportConfig& cfg, std::string_view idOrNominal, std::string& error)
	{
		std::string canon;
		if (!resolveStrategy(cfg, idOrNominal, canon, error)) return false;
		m_active = canon;
		m_assign.clear();
		m_source = StrategySource::Manual;
		m_manualOverride = true;
		return true;
	}

	bool StrategyBook::assign(const AirportConfig& cfg, const std::vector<std::string_view>& params, std::string& error)
	{
		if (params.empty()) { error = "Nothing to assign"; return false; }
		if (params.size() == 1 && lower(params[0]) == "clear")
		{
			m_assign.clear();
			m_source = StrategySource::Manual;
			m_manualOverride = true;
			return true;
		}
		std::map<std::string, std::string> staged = m_assign;
		for (const auto& param : params)
		{
			const auto eq = param.find('=');
			if (eq == std::string_view::npos) { error = "Expected WAYPOINT=mode, got '" + std::string(param) + "'"; return false; }
			std::string wpt, mode;
			if (!checkPair(cfg, param.substr(0, eq), param.substr(eq + 1), wpt, mode, error)) return false;
			staged[wpt] = mode;
		}
		m_assign = std::move(staged);
		m_source = StrategySource::Manual;
		m_manualOverride = true;
		return true;
	}

	void StrategyBook::resume()
	{
		m_manualOverride = false;
	}

	std::vector<std::string> StrategyBook::pruneInvalid(const AirportConfig& cfg)
	{
		std::vector<std::string> dropped;
		for (auto it = m_assign.begin(); it != m_assign.end();)
		{
			const std::string canon = hasWaypoint(cfg, it->first) ? canonicalMode(cfg, it->second) : "";
			if (canon.empty())
			{
				dropped.push_back(it->first + "=" + it->second);
				it = m_assign.erase(it);
			}
			else
			{
				it->second = canon;
				++it;
			}
		}
		return dropped;
	}

	bool StrategyBook::applyExternal(const AirportConfig& cfg, const nlohmann::json& value, std::string& error)
	{
		if (!value.is_object()) { error = "Strategy value must be an object"; return false; }

		const bool hasStrategy = value.contains("strategy");
		const bool hasAssign = value.contains("assign");

		std::string canon;
		if (hasStrategy)
		{
			if (!value["strategy"].is_string()) { error = "'strategy' must be a string"; return false; }
			if (!resolveStrategy(cfg, value["strategy"].get<std::string>(), canon, error)) return false;
		}
		std::map<std::string, std::string> staged;
		if (hasAssign)
		{
			const auto& assignJson = value["assign"];
			if (!assignJson.is_object()) { error = "'assign' must be an object"; return false; }
			for (auto it = assignJson.begin(); it != assignJson.end(); ++it)
			{
				if (!it.value().is_string()) { error = "Mode of '" + it.key() + "' must be a string"; return false; }
				std::string wpt, mode;
				if (!checkPair(cfg, it.key(), it.value().get<std::string>(), wpt, mode, error)) return false;
				staged[wpt] = mode;
			}
		}

		if (m_manualOverride) return true; // valid but ignored while a controller drives it
		if (!hasStrategy && !hasAssign) return true;

		if (hasStrategy)
		{
			m_active = canon;
			m_assign.clear();
		}
		if (hasAssign) m_assign = std::move(staged);
		m_source = StrategySource::External;
		return true;
	}

	std::string StrategyBook::modeFor(const AirportConfig& cfg, std::string_view waypoint) const
	{
		const std::string wpt = upper(waypoint);
		const auto it = m_assign.find(wpt);
		if (it != m_assign.end()) return it->second;
		if (!m_active.empty())
		{
			if (const Strategy* strategy = cfg.findStrategy(m_active))
			{
				for (const auto& pair : strategy->assign)
					if (upper(pair.first) == wpt) return pair.second;
				for (const auto& pair : strategy->assign)
					if (pair.first == "*") return pair.second;
			}
		}
		return "default";
	}

	nlohmann::ordered_json StrategyBook::toBridge(const AirportConfig& cfg) const
	{
		using Json = nlohmann::ordered_json;
		Json out = Json::object();
		const Strategy* strategy = m_active.empty() ? nullptr : cfg.findStrategy(m_active);
		out["strategy"] = strategy ? strategy->id : "nominal";
		out["label"] = strategy ? strategy->label : "Nominal";
		Json assignJson = Json::object();
		for (const auto& pair : m_assign) assignJson[pair.first] = pair.second;
		out["assign"] = assignJson;
		out["source"] = m_source == StrategySource::External ? "external" : "manual";

		// every waypoint whose resolved mode is not "default" ("*" expanded); missing means default
		Json effective = Json::object();
		for (const auto& entry : cfg.entries)
		{
			if (effective.contains(entry.waypoint)) continue;
			const std::string mode = modeFor(cfg, entry.waypoint);
			if (mode != "default") effective[entry.waypoint] = mode;
		}
		out["effective"] = effective;
		return out;
	}
}
