#pragma once

#include "sidconfigv2.h"
#include "include/nlohmann/json.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vsid::v2
{
	enum class StrategySource { Manual, External };

	// Runtime strategy state of one airport: active strategy plus per-waypoint overrides.
	class StrategyBook
	{
	public:
		void reset(const AirportConfig& cfg);
		bool setStrategy(const AirportConfig& cfg, std::string_view idOrNominal, std::string& error);
		bool assign(const AirportConfig& cfg, const std::vector<std::string_view>& params, std::string& error);
		void resume();
		// Drops the overrides whose waypoint or mode the config no longer has (after a reload).
		// Returns the dropped pairs as "WPT=mode", in waypoint order.
		std::vector<std::string> pruneInvalid(const AirportConfig& cfg);
		bool applyExternal(const AirportConfig& cfg, const nlohmann::json& value, std::string& error);
		std::string modeFor(const AirportConfig& cfg, std::string_view waypoint) const;
		nlohmann::ordered_json toBridge(const AirportConfig& cfg) const;
		const std::string& active() const { return m_active; }
		bool manualOverride() const { return m_manualOverride; }

	private:
		std::string m_active;                       // "" = nominal
		std::map<std::string, std::string> m_assign; // waypoint -> mode
		StrategySource m_source = StrategySource::Manual;
		bool m_manualOverride = false;
	};
}
