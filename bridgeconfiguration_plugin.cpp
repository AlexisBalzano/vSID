#include "pch.h"
#include "vSIDPlugin.h"
#include "bridgeconfiguration.h"
#include "flightplan.h"

void vsid::VSIDPlugin::publishBridgeConfiguration()
{
    if (!bridgeApi_ || !bridgeProvider_) return;
    std::string paris, automaticModes, taxi = "?";
    for (const auto name : bridgeconfig::Airports)
    {
        const auto airport = activeAirports.find(name);
        if (airport == activeAirports.end()) continue;
        paris += bridgeconfig::paris(name, airport->second.customRules,
            autoConfiguration.enabled && !autoConfiguration.isManual(airport->first));
    }
    for (const auto& [icao, airport] : activeAirports)
    {
        if (icao.size() != 4) continue;
        const auto automatic = airport.settings.find("auto");
        if (automatic != airport.settings.end())
            automaticModes += icao + (automatic->second ? "=1;" : "=0;");
    }
    // Never send a truncated snapshot that could masquerade as complete.
    if (automaticModes.size() > 4096) automaticModes.clear();
    if (const auto airport = activeAirports.find("LFPG"); airport != activeAirports.end())
    {
        std::optional<bool> north, south;
        bool otherActive = false;
        for (const auto& [name, area] : airport->second.areas)
        {
            if (utils::svEqualCi(name, "NORTH")) north = area.isActive;
            else if (utils::svEqualCi(name, "SOUTH")) south = area.isActive;
            else otherActive = otherActive || area.isActive;
        }
        taxi = bridgeconfig::taxi(north, south, otherActive);
    }
    const auto publish = [&](ESB_FieldId field, const std::string& value)
    {
        const auto previous = bridgeConfigurationCache_.find(field);
        if (previous != bridgeConfigurationCache_.end() && previous->second == value) return;
        const auto data = ESB_Str(value.c_str());
        if (bridgeApi_->set_global(bridgeProvider_, field, &data) == ESB_OK)
            bridgeConfigurationCache_[field] = value;
    };
    publish(bridgeParisField_, paris);
    publish(bridgeAutomaticModeField_, automaticModes);
    publish(bridgeTaxiField_, taxi);
}

bool vsid::VSIDPlugin::handleParisConfigurationCommand(const vsid::Command& command)
{
    if (!utils::svEqualCi(command.command, "paris")) return false;
    if (command.params.size() != 2)
    {
        Logger::log(LogLevel::Info, "Usage: .vsid paris ICAO linked|unlinked|wlpg|elpg|wipg|eipg");
        return true;
    }
    const auto icao = utils::toupper(std::string(command.params[0]));
    const auto selection = utils::toupper(std::string(command.params[1]));
    const auto airport = activeAirports.find(icao);
    if (airport == activeAirports.end() || !bridgeconfig::select(airport->second.customRules, icao, selection))
    {
        Logger::log(LogLevel::Info, std::format("[{}] Cannot select {}: airport inactive, unsupported selection or required rules missing.", icao, selection));
        return true;
    }
    rememberManualConfiguration(icao);
    publishBridgeConfiguration();
    Logger::log(LogLevel::Info, std::format("[{}] Manual configuration: {}", icao, selection));
    for (const auto& [callsign, info] : processed)
    {
        auto flight = FlightPlanSelect(callsign.c_str());
        if (!flight.IsValid() || icao != flight.GetFlightPlanData().GetOrigin()) continue;
        const auto block = fplnhelper::getAtcBlock(flight);
        processFlightplan(flight, true, block.second);
    }
    return true;
}
