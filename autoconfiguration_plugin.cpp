#include "pch.h"
#include "vSIDPlugin.h"
#include "flightplan.h"
#include <filesystem>
#include <fstream>

bool vsid::VSIDPlugin::loadAutoConfiguration()
{
    this->autoConfigurationLoadAttempted = true;
    wchar_t modulePath[32768] = {};
    const auto length = GetModuleFileNameW((HINSTANCE)&__ImageBase, modulePath, 32768);
    if (length == 0 || length >= 32768)
    {
        Logger::log(LogLevel::Error, "Cannot locate vSidAutoConfig.json beside vSID.dll; previous mappings retained.");
        return false;
    }
    const auto path = std::filesystem::path(modulePath).parent_path() / "vSidAutoConfig.json";
    std::ifstream input(path);
    std::string error;
    if (!this->autoConfiguration.load(input, error))
    {
        Logger::log(LogLevel::Error, std::format("Auto configuration [{}]: {}. {}", path.string(), error,
            this->autoConfiguration.loaded() ? "Previous mappings retained." : "No mappings loaded; automatic selection unavailable."));
        return false;
    }
    Logger::log(LogLevel::Info, std::format("Loaded Auto configuration [{}]", path.string()));
    return true;
}

void vsid::VSIDPlugin::rememberManualConfiguration(std::string_view icao)
{
    const auto it = this->activeAirports.find(icao);
    if (it == this->activeAirports.end()) return;
    const autoconfig::Rules rules(it->second.customRules.begin(), it->second.customRules.end());
    if (!this->autoConfiguration.detect(it->first, {}, rules).supported) return;
    const bool wasManual = this->autoConfiguration.isManual(it->first);
    this->autoConfiguration.setManual(it->first, rules);
    if (this->autoConfiguration.enabled && !wasManual)
        Logger::log(LogLevel::Info, std::format("[{}] Auto configuration suspended by manual rule selection. Use .vsid autoconfig {} auto to resume.", it->first, it->first));
}

void vsid::VSIDPlugin::updateAutoConfiguration(bool refreshSuggestions)
{
    if (!this->autoConfigurationLoadAttempted) this->loadAutoConfiguration();
    autoconfig::Snapshot snapshot;
    this->SelectActiveSectorfile();
    // Reference-airport observations are independent of which airports the controller activates.
    for (auto element = this->SectorFileElementSelectFirst(EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY);
        element.IsValid(); element = this->SectorFileElementSelectNext(element, EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY))
    {
        const auto icao = utils::toupper(utils::trim(element.GetAirportName()));
        if (!this->autoConfiguration.observes(icao)) continue;
        for (int end = 0; end < 2; ++end)
        {
            const auto runway = utils::toupper(utils::trim(element.GetRunwayName(end)));
            if (element.IsElementActive(false, end)) snapshot[icao].arrivals.insert(runway);
            if (element.IsElementActive(true, end)) snapshot[icao].departures.insert(runway);
        }
    }

    const auto previousStatus = this->autoConfiguration.statuses();
    std::set<std::string> changed;
    for (auto& [icao, airport] : this->activeAirports)
    {
        autoconfig::Rules rules(airport.customRules.begin(), airport.customRules.end());
        if (this->autoConfiguration.apply(icao, snapshot, rules))
        {
            for (const auto& [key, value] : rules) airport.customRules.at(key) = value;
            changed.insert(icao);
        }
        const auto& status = this->autoConfiguration.statuses().at(icao);
        if ((this->autoConfiguration.enabled || this->autoConfiguration.isManual(icao)) &&
            (!previousStatus.contains(icao) || previousStatus.at(icao) != status))
            Logger::log(LogLevel::Info, std::format("[{}] Auto configuration: {}", icao, status));
    }

    publishBridgeConfiguration();
    if (!refreshSuggestions || changed.empty()) return;
    // Refresh suggestions only. This feature never grants a clearance or assigns a runway.
    for (const auto& [callsign, info] : this->processed)
    {
        auto flight = this->FlightPlanSelect(callsign.c_str());
        if (!flight.IsValid() || !changed.contains(flight.GetFlightPlanData().GetOrigin())) continue;
        const auto block = fplnhelper::getAtcBlock(flight);
        this->processFlightplan(flight, true, block.second);
    }
}

bool vsid::VSIDPlugin::handleAutoConfigurationCommand(const vsid::Command& command)
{
    if (!utils::svEqualCi(command.command, "autoconfig")) return false;
    if (!this->autoConfigurationLoadAttempted &&
        !(command.params.size() == 1 && utils::svEqualCi(command.params[0], "reload")))
        this->loadAutoConfiguration();
    const auto usage = [] {
        Logger::log(LogLevel::Info, "Usage: .vsid autoconfig [on|off|status|reload] or .vsid autoconfig ICAO [auto|manual]");
    };
    std::string filter;
    if (command.params.size() == 1)
    {
        if (utils::svEqualCi(command.params[0], "reload"))
        {
            if (!this->loadAutoConfiguration()) return true;
            this->updateAutoConfiguration(true);
        }
        else if (utils::svEqualCi(command.params[0], "on"))
        {
            if (!this->autoConfiguration.loaded() && !this->loadAutoConfiguration()) return true;
            this->autoConfiguration.enabled = true;
            if (this->activeAirports.empty()) this->UpdateActiveAirports();
            else this->updateAutoConfiguration(true);
        }
        else if (utils::svEqualCi(command.params[0], "off"))
        {
            this->autoConfiguration.enabled = false;
            this->updateAutoConfiguration(false);
        }
        else if (!utils::svEqualCi(command.params[0], "status"))
            filter = utils::toupper(std::string(command.params[0]));
    }
    else if (command.params.size() == 2)
    {
        filter = utils::toupper(std::string(command.params[0]));
        const auto it = this->activeAirports.find(filter);
        if (it == this->activeAirports.end())
        {
            Logger::log(LogLevel::Info, std::format("[{}] is not an active airport.", filter));
            return true;
        }
        const autoconfig::Rules rules(it->second.customRules.begin(), it->second.customRules.end());
        const auto decision = this->autoConfiguration.detect(filter, {}, rules);
        if (!decision.supported)
        {
            Logger::log(LogLevel::Info, std::format("[{}] {}", filter, decision.status));
            return true;
        }
        if (utils::svEqualCi(command.params[1], "auto"))
        {
            this->autoConfiguration.resume(filter);
            this->autoConfiguration.enabled = true;
        }
        else if (utils::svEqualCi(command.params[1], "manual"))
            this->autoConfiguration.setManual(filter, rules);
        else { usage(); return true; }
        this->updateAutoConfiguration(true);
    }
    else if (!command.params.empty()) { usage(); return true; }

    Logger::log(LogLevel::Info, std::format("Auto configuration {} (independent of SID assignment Auto).",
        this->autoConfiguration.enabled ? "ON" : "OFF"));
    if (!filter.empty() && !this->activeAirports.contains(filter))
    {
        Logger::log(LogLevel::Info, std::format("[{}] is not an active airport.", filter));
        usage();
        return true;
    }
    for (const auto& [icao, airport] : this->activeAirports)
    {
        if (!filter.empty() && filter != icao) continue;
        const auto& statuses = this->autoConfiguration.statuses();
        std::string rules;
        for (const auto& [key, value] : airport.customRules)
            rules += key + "=" + (value ? "ON " : "OFF ");
        Logger::log(LogLevel::Info, std::format("[{}] {} | {}", icao,
            statuses.contains(icao) ? statuses.at(icao) : "OFF", rules));
    }
    return true;
}
