#include "pch.h"
#include "vSIDPlugin.h"
#include "bridgeconfiguration.h"
#include "flightplan.h"

/** @brief Publish actual rules and areas for every active airport without regional policy. */
void vsid::VSIDPlugin::publishBridgeConfiguration()
{
	if (!this->bridgeApi_ || !this->bridgeProvider_) return;
	bridgeconfig::Snapshot snapshot;
	for (const auto& [icao, airport] : this->activeAirports)
	{
		snapshot.rules[icao] = { airport.customRules.begin(), airport.customRules.end() };
		auto& areas = snapshot.areas[icao];
		for (const auto& [name, area] : airport.areas) areas[name] = area.isActive;
		const auto automatic = airport.settings.find("auto");
		if (automatic != airport.settings.end()) snapshot.assignmentAuto[icao] = automatic->second;
		const auto& states = this->autoConfiguration.statuses();
		snapshot.states[icao] = states.contains(icao) ? states.at(icao) : autoconfig::State{};
		if (this->autoConfiguration.isManual(icao)) snapshot.manualAirports.insert(icao);
	}
	const auto values = bridgeconfig::serialize(snapshot);
	const auto publish = [&](ESB_FieldId field, const std::string& name, std::size_t limit)
	{
		const auto result = bridgeconfig::publish(field, values.at(name), limit, this->bridgeConfigurationCache_,
			[&](const std::string& value)
			{
				const auto data = ESB_Str(value.c_str());
				return this->bridgeApi_->set_global(this->bridgeProvider_, field, &data) == ESB_OK;
			});
		if (result == bridgeconfig::Publication::Overflow)
		{
			if (this->bridgeConfigurationWarnings_.insert(field).second)
				Logger::log(LogLevel::Warning, "Bridge " + name + " exceeds its byte limit; previous snapshot retained.");
		}
		else this->bridgeConfigurationWarnings_.erase(field);
	};
	publish(this->bridgeRulesField_, "rules", bridgeconfig::ConfigurationMaxBytes);
	publish(this->bridgeAreasField_, "areas", bridgeconfig::ConfigurationMaxBytes);
	publish(this->bridgeAutoConfigField_, "autoconfig", bridgeconfig::ConfigurationMaxBytes);
	publish(this->bridgeAutomaticModeField_, "automode", bridgeconfig::AutomaticModeMaxBytes);
}

/** @brief Generic, atomic counterpart to the existing rule toggle command. */
bool vsid::VSIDPlugin::handleRulesConfigurationCommand(const vsid::Command& command)
{
	if (!utils::svEqualCi(command.command, "rules")) return false;
	if (command.params.size() < 2)
	{
		Logger::log(LogLevel::Info, "Usage: .vsid rules ICAO KEY=on KEY2=off ... (existing rules only)");
		return true;
	}
	const auto icao = utils::toupper(std::string(command.params[0]));
	const auto airport = this->activeAirports.find(icao);
	std::vector<std::string_view> assignments(command.params.begin() + 1, command.params.end());
	std::vector<std::string> selected;
	if (airport == this->activeAirports.end() || !bridgeconfig::assign(airport->second.customRules, assignments, selected))
	{
		Logger::log(LogLevel::Info, "[" + icao + "] Rules unchanged: inactive airport, unknown/duplicate key or invalid assignment.");
		return true;
	}
	for (const auto& key : selected) this->rememberManualConfiguration(icao, key);
	this->publishBridgeConfiguration();
	Logger::log(LogLevel::Info, "[" + icao + "] Explicit rule selection applied.");
	for (const auto& [callsign, info] : this->processed)
	{
		auto flight = this->FlightPlanSelect(callsign.c_str());
		if (!flight.IsValid() || icao != flight.GetFlightPlanData().GetOrigin()) continue;
		const auto block = fplnhelper::getAtcBlock(flight);
		this->processFlightplan(flight, true, block.second);
	}
	return true;
}
