#pragma once

#include "autoconfiguration.h"
#include "include/nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace vsid::bridgeconfig
{
	inline constexpr std::uint32_t ConfigurationMaxBytes = 65536;
	inline constexpr std::uint32_t AutomaticModeMaxBytes = 4096;
	using AirportRules = std::map<std::string, autoconfig::Rules>;

	struct Snapshot
	{
		AirportRules rules;
		AirportRules areas;
		std::map<std::string, bool> assignmentAuto;
		std::map<std::string, autoconfig::State> states;
		std::set<std::string> manualAirports;
	};

	/** @brief Complete generic snapshots; JSON escapes arbitrary rule/profile names. */
	inline std::map<std::string, std::string> serialize(const Snapshot& snapshot)
	{
		nlohmann::json states = nlohmann::json::object();
		for (const auto& [icao, state] : snapshot.states)
			states[icao] = {
				{ "status", autoconfig::statusName(state.code) },
				{ "profile", state.profile }, { "detail", state.detail },
				{ "manual", snapshot.manualAirports.contains(icao) }
			};
		std::string automatic;
		for (const auto& [icao, enabled] : snapshot.assignmentAuto)
			if (icao.size() == 4) automatic += icao + (enabled ? "=1;" : "=0;");
		return {
			{ "rules", nlohmann::json(snapshot.rules).dump() },
			{ "areas", nlohmann::json(snapshot.areas).dump() },
			{ "autoconfig", states.dump() },
			{ "automode", automatic }
		};
	}

	enum class Publication { Unchanged, Overflow, Published, Failed };

	/** @brief Never replace a valid publication with an overflow placeholder. */
	template<class Field, class Writer>
	Publication publish(Field field, const std::string& value, std::size_t limit,
		std::map<Field, std::string>& cache, Writer write)
	{
		if (value.size() > limit) return Publication::Overflow;
		const auto previous = cache.find(field);
		if (previous != cache.end() && previous->second == value) return Publication::Unchanged;
		if (!write(value)) return Publication::Failed;
		cache[field] = value;
		return Publication::Published;
	}

	/** @brief Validate a complete explicit rule assignment before changing any rule. */
	template<class Rules, class Parameters>
	bool assign(Rules& rules, const Parameters& parameters, std::vector<std::string>& selected)
	{
		std::map<std::string, bool> changes;
		for (const auto parameter : parameters)
		{
			const std::string token(parameter);
			const auto separator = token.find('=');
			if (separator == std::string::npos || separator == 0) return false;
			auto key = token.substr(0, separator);
			auto value = token.substr(separator + 1);
			for (auto* text : { &key, &value })
				std::transform(text->begin(), text->end(), text->begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			if (value != "ON" && value != "OFF" && value != "1" && value != "0") return false;
			if (!rules.contains(key) || !changes.emplace(key, value == "ON" || value == "1").second) return false;
		}
		if (changes.empty()) return false;
		for (const auto& [key, value] : changes)
		{
			rules.at(key) = value;
			selected.push_back(key);
		}
		return true;
	}
}
