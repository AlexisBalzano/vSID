#include "pch.h"
#include "vSIDPlugin.h"
#include "flightplan.h"
#include <filesystem>
#include <fstream>

bool vsid::VSIDPlugin::loadAutoConfiguration()
{
	this->autoConfigurationLoadAttempted = true;
	const auto directory = this->configParser.airportConfigDirectory();
	if (directory.empty())
	{
		Logger::log(LogLevel::Error, "Cannot locate Auto configuration: airportConfigs is not set.");
		return false;
	}
	const auto path = directory / "vSidAutoConfig.json";
	std::ifstream input(path);
	std::string error;
	if (!this->autoConfiguration.load(input, error))
	{
		Logger::log(LogLevel::Error, std::format("Auto configuration [{}]: {}. {}", path.string(), error,
			this->autoConfiguration.loaded() ? "Previous mappings retained." : "Install vSidAutoConfig.json in the Airport Config directory; automatic selection unavailable."));
		return false;
	}
	Logger::log(LogLevel::Info, std::format("Loaded Auto configuration [{}]", path.string()));
	return true;
}

void vsid::VSIDPlugin::rememberManualConfiguration(std::string_view icao, std::string_view key)
{
	const auto it = this->activeAirports.find(icao);
	if (it == this->activeAirports.end()) return;
	const autoconfig::Rules rules(it->second.customRules.begin(), it->second.customRules.end());
	const bool wasManual = this->autoConfiguration.isManual(it->first);
	if (!this->autoConfiguration.rememberRuleChange(it->first, std::string(key), rules)) return;
	if (!wasManual)
		Logger::log(LogLevel::Info, std::format("[{}] Auto configuration suspended by manual rule selection. Use .vsid autoconfig {} auto to resume.", it->first, it->first));
}

void vsid::VSIDPlugin::updateAutoConfiguration(bool refreshFlights)
{
	if (!this->autoConfigurationLoadAttempted) this->loadAutoConfiguration();
	autoconfig::Snapshot snapshot;
	autoconfig::RunwayInventory inventory;
	bool sectorHasRunways = false;
	this->SelectActiveSectorfile();
	// Reference-airport observations are independent of which airports the controller activates.
	for (auto element = this->SectorFileElementSelectFirst(EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY);
		element.IsValid(); element = this->SectorFileElementSelectNext(element, EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY))
	{
		sectorHasRunways = true;
		const auto icao = utils::toupper(utils::trim(element.GetAirportName()));
		if (!this->autoConfiguration.observes(icao)) continue;
		for (int end = 0; end < 2; ++end)
		{
			const auto runway = utils::toupper(utils::trim(element.GetRunwayName(end)));
			inventory[icao].insert(runway);
			if (element.IsElementActive(false, end)) snapshot[icao].arrivals.insert(runway);
			if (element.IsElementActive(true, end)) snapshot[icao].departures.insert(runway);
		}
	}

	// Report missing configured runways once per inventory/configuration change.
	std::set<std::string> warnings;
	for (const auto& runway : sectorHasRunways ? this->autoConfiguration.missingRunways(inventory) : std::vector<std::string>{})
	{
		warnings.insert(runway);
		if (!this->autoConfigurationRunwayWarnings.contains(runway))
			Logger::log(LogLevel::Warning, "Auto configuration runway not found in the active sector file: " + runway);
	}
	this->autoConfigurationRunwayWarnings = std::move(warnings);

	const auto previousStatus = this->autoConfiguration.statuses();
	std::set<std::string> changed;
	for (auto& [icao, airport] : this->activeAirports)
	{
		autoconfig::Rules rules(airport.customRules.begin(), airport.customRules.end());
		if (this->autoConfiguration.apply(icao, snapshot, rules))
		{
			for (const auto& [key, value] : rules) airport.customRules.at(key) = value;
			changed.insert(utils::toupper(icao));
		}
		const auto key = utils::toupper(icao);
		const auto status = this->autoConfiguration.stateFor(icao);
		if ((this->autoConfiguration.enabled || this->autoConfiguration.isManual(icao)) &&
			(!previousStatus.contains(key) || previousStatus.at(key) != status))
			Logger::log(LogLevel::Info, std::format("[{}] Auto configuration: {}", icao, autoconfig::describe(status)));
	}

	this->publishBridgeConfiguration();
	if (refreshFlights && !changed.empty()) this->refreshRuleConfiguration(changed);
}

bool vsid::VSIDPlugin::handleAutoConfigurationCommand(const vsid::Command& command)
{
	if (!utils::svEqualCi(command.command, "autoconfig")) return false;
	if (!this->autoConfigurationLoadAttempted &&
		!(command.params.size() == 1 && utils::svEqualCi(command.params[0], "reload")))
		this->loadAutoConfiguration();
	const auto usage = []
	{
		Logger::log(LogLevel::Info, "Usage: .vsid autoconfig [on|off|status|reload] or .vsid autoconfig ICAO [auto|manual] or .vsid autoconfig resume all");
	};
	std::string filter;
	if (command.params.size() == 2 && utils::svEqualCi(command.params[0], "resume") && utils::svEqualCi(command.params[1], "all"))
	{
		this->autoConfiguration.resumeAll();
		this->updateAutoConfiguration(true);
	}
	else if (command.params.size() == 1)
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
		if (!decision.supported && !utils::svEqualCi(command.params[1], "auto"))
		{
			Logger::log(LogLevel::Info, std::format("[{}] {}", filter, autoconfig::describe(decision.state)));
			return true;
		}
		if (utils::svEqualCi(command.params[1], "auto"))
		{
			this->autoConfiguration.resume(filter);
		}
		else if (utils::svEqualCi(command.params[1], "manual"))
		{
			if (!this->autoConfiguration.setManual(filter, rules))
				Logger::log(LogLevel::Info, "[" + filter + "] No complete managed-rule set to override.");
		}
		else { usage(); return true; }
		this->updateAutoConfiguration(true);
	}
	else if (!command.params.empty()) { usage(); return true; }

	Logger::log(LogLevel::Info, std::format("Auto configuration {} (independent of SID assignment Auto).",
		this->autoConfiguration.enabled ? "ON" : "OFF"));
	const auto manual = this->autoConfiguration.manualAirports();
	if (!manual.empty())
	{
		std::string list;
		for (const auto& icao : manual) list += icao + " ";
		Logger::log(LogLevel::Info, "Manual overrides retained (including inactive airports): " + list);
	}
	if (!filter.empty() && !this->activeAirports.contains(filter))
	{
		Logger::log(LogLevel::Info, std::format("[{}] is not an active airport.", filter));
		usage();
		return true;
	}
	for (const auto& [icao, airport] : this->activeAirports)
	{
		const auto key = utils::toupper(icao);
		if (!filter.empty() && filter != key) continue;
		std::string rules;
		for (const auto& [key, value] : airport.customRules)
			rules += key + "=" + (value ? "ON " : "OFF ");
		Logger::log(LogLevel::Info, std::format("[{}] {} | {}", icao,
			autoconfig::describe(this->autoConfiguration.stateFor(icao)), rules));
	}
	return true;
}
