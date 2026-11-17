#include "pch.h"
#include "vSIDPlugin.h"
#include "bridgeconfiguration.h"

#include <functional>

namespace
{
	constexpr int StrategySourceRetrySeconds = 30;

	void __cdecl strategySourceChanged(ESB_FieldId, ESB_Aircraft, void* user)
	{
		static_cast<vsid::VSIDPlugin*>(user)->onStrategySourceChanged();
	}

	std::string strategyLabel(const vsid::Airport& airport)
	{
		return airport.strategy.toBridge(airport.sidConfig).at("label").get<std::string>();
	}

	// "<label> (<source>), overrides: <WPT=mode,...|none>"
	std::string strategyState(const vsid::Airport& airport)
	{
		const auto state = airport.strategy.toBridge(airport.sidConfig);
		std::string overrides;
		for (const auto& [waypoint, mode] : state.at("assign").items())
			overrides += (overrides.empty() ? "" : ",") + waypoint + "=" + mode.get<std::string>();
		return state.at("label").get<std::string>() + " (" + state.at("source").get<std::string>() +
			"), overrides: " + (overrides.empty() ? "none" : overrides);
	}
}

/** @brief ".vsid strategy ICAO [id|nominal|resume]" and ".vsid assign ICAO WPT=mode ...|clear". */
bool vsid::VSIDPlugin::handleStrategyCommand(const vsid::Command& command)
{
	const bool isStrategy = utils::svEqualCi(command.command, "strategy");
	const bool isAssign = utils::svEqualCi(command.command, "assign");
	if (!isStrategy && !isAssign) return false;
	if (command.params.empty() || (isStrategy && command.params.size() > 2) || (isAssign && command.params.size() < 2))
	{
		Logger::log(LogLevel::Info, "Usage: .vsid strategy ICAO [id|nominal|resume] / .vsid assign ICAO WPT=mode ... / .vsid assign ICAO clear");
		return true;
	}
	const auto icao = utils::toupper(std::string(command.params[0]));
	const auto found = this->activeAirports.find(icao);
	if (found == this->activeAirports.end())
	{
		Logger::log(LogLevel::Info, "[" + icao + "] Strategy unchanged: inactive airport");
		return true;
	}
	auto& airport = found->second;

	if (isStrategy && command.params.size() == 1)
	{
		Logger::log(LogLevel::Info, "[" + icao + "] Strategy " + strategyState(airport));
		return true;
	}

	if (isStrategy && utils::svEqualCi(command.params[1], "resume"))
	{
		airport.strategy.resume();
		if (this->strategySourceName_.empty())
		{
			Logger::log(LogLevel::Info, "[" + icao + "] Strategy handed back to the external source, but none is configured.");
			return true;
		}
		const auto changed = this->applyStrategySource();
		Logger::log(LogLevel::Info, "[" + icao + "] Strategy handed back to " + this->strategySourceName_ + ", now " + strategyState(airport));
		if (!changed.empty())
		{
			this->publishBridgeConfiguration();
			this->refreshRuleConfiguration(changed);
		}
		return true;
	}

	std::string error;
	const bool applied = isStrategy ?
		airport.strategy.setStrategy(airport.sidConfig, command.params[1], error) :
		airport.strategy.assign(airport.sidConfig, { command.params.begin() + 1, command.params.end() }, error);
	if (!applied)
	{
		Logger::log(LogLevel::Info, "[" + icao + "] Strategy unchanged: " + error);
		return true;
	}
	this->publishBridgeConfiguration();
	Logger::log(LogLevel::Info, "[" + icao + "] Strategy " + strategyLabel(airport) + " applied.");
	this->refreshRuleConfiguration({ icao });
	return true;
}

/** @brief Subscribe to the configured external strategy field; retried every 30 s until found. */
void vsid::VSIDPlugin::connectStrategySource(int counter)
{
	const std::string& wanted = this->configParser.strategySource;
	if (wanted != this->strategySourceName_)
	{
		// Source changed by a main config reload: drop the old subscription and start over
		if (this->strategySourceSub_ != nullptr && this->bridgeApi_ != nullptr) this->bridgeApi_->unsubscribe(this->strategySourceSub_);
		this->strategySourceSub_ = nullptr;
		this->strategySourceField_ = ESB_FIELD_NONE;
		this->strategySourceName_ = wanted;
		this->strategySourceNextAttempt_ = 0;
		this->strategySourceMissingLogged_ = false;
		this->strategySourceWarnings_.clear();
	}
	if (this->strategySourceName_.empty() || this->bridgeApi_ == nullptr || this->strategySourceSub_ != nullptr) return;
	if (counter < this->strategySourceNextAttempt_) return;
	this->strategySourceNextAttempt_ = counter + StrategySourceRetrySeconds;

	ESB_FieldId field = ESB_FIELD_NONE;
	ESB_Status status = this->bridgeApi_->resolve(this->strategySourceName_.c_str(), ESB_T_STR, &field);
	ESB_Sub* subscription = nullptr;
	if (status == ESB_OK) status = this->bridgeApi_->subscribe(field, &strategySourceChanged, this, ESB_SelfModule(), &subscription);
	if (status != ESB_OK)
	{
		if (!this->strategySourceMissingLogged_)
			Logger::log(LogLevel::Info, "Strategy source " + this->strategySourceName_ + " not available on the bridge (status " +
				std::to_string(status) + "). Retrying every 30 s.");
		this->strategySourceMissingLogged_ = true;
		return;
	}
	this->strategySourceField_ = field;
	this->strategySourceSub_ = subscription;
	Logger::log(LogLevel::Info, "Strategy source " + this->strategySourceName_ + " connected.");
	this->onStrategySourceChanged(); // the field may already hold a value
}

/** @brief Read the external strategy value and apply it to the active airports it names. */
std::set<std::string> vsid::VSIDPlugin::applyStrategySource()
{
	std::set<std::string> changed;
	if (this->bridgeApi_ == nullptr || this->strategySourceField_ == ESB_FIELD_NONE) return changed;

	std::vector<char> buffer(bridgeconfig::ConfigurationMaxBytes);
	ESB_Value value = {};
	uint32_t bytes = static_cast<uint32_t>(buffer.size());
	ESB_Status status = this->bridgeApi_->get_global(this->strategySourceField_, &value, buffer.data(), &bytes);
	if (status == ESB_E_BUFFER_TOO_SMALL)
	{
		buffer.resize(bytes);
		value = {};
		status = this->bridgeApi_->get_global(this->strategySourceField_, &value, buffer.data(), &bytes);
	}
	if (status == ESB_E_UNSET) return changed;
	if (status != ESB_OK)
	{
		Logger::log(LogLevel::Debug, "Strategy source " + this->strategySourceName_ + " unreadable (status " + std::to_string(status) + ").", DebugLevel::Conf);
		return changed;
	}
	const char* data = value.v.ptr != nullptr ? static_cast<const char*>(value.v.ptr) : buffer.data();
	const std::string raw(data, value.bytes);

	std::vector<std::string> errors;
	const auto parsed = nlohmann::json::parse(raw, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) errors.push_back("not a JSON object");
	else
	{
		std::map<std::string, const nlohmann::json*> byAirport;
		for (const auto& [key, airportValue] : parsed.items()) byAirport[utils::toupper(key)] = &airportValue;
		for (auto& [key, airport] : this->activeAirports)
		{
			const auto icao = utils::toupper(key);
			const auto entry = byAirport.find(icao);
			if (entry == byAirport.end()) continue; // absent airports are left alone
			const auto before = airport.strategy.toBridge(airport.sidConfig).dump();
			std::string error;
			if (!airport.strategy.applyExternal(airport.sidConfig, *entry->second, error)) errors.push_back("[" + icao + "] " + error);
			else if (airport.strategy.toBridge(airport.sidConfig).dump() != before)
			{
				changed.insert(icao);
				Logger::log(LogLevel::Info, "[" + icao + "] Strategy " + strategyLabel(airport) + " applied from " + this->strategySourceName_ + ".");
			}
		}
	}
	if (!errors.empty() && this->strategySourceWarnings_.insert(std::hash<std::string>{}(raw)).second)
	{
		std::string reasons;
		for (const auto& error : errors) reasons += (reasons.empty() ? "" : "; ") + error;
		Logger::log(LogLevel::Warning, "Strategy source " + this->strategySourceName_ + " value ignored: " + reasons);
	}
	return changed;
}

void vsid::VSIDPlugin::onStrategySourceChanged()
{
	const auto changed = this->applyStrategySource();
	if (changed.empty()) return;
	this->publishBridgeConfiguration();
	this->refreshRuleConfiguration(changed);
}
