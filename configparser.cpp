#include "pch.h"

#include "configparser.h"
#include "utils.h"
#include "messageHandler.h"
#include "sid.h"
#include "constants.h"
#include "logger.h"

#include <vector>
#include <fstream>
#include <format>

vsid::ConfigParser::ConfigParser()
{
};

vsid::ConfigParser::~ConfigParser() = default;

void vsid::ConfigParser::loadMainConfig()
{
	char path[MAX_PATH + 1] = { 0 };
	GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
	PathRemoveFileSpecA(path);
	std::filesystem::path basePath = path;

	std::ifstream configFile(basePath.append("vSidConfig.json").string());

	try
	{
		this->vSidConfig = json::parse(configFile);
	}
	catch(const json::parse_error &e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to parse main config: " + std::string(e.what()));
	}
	catch (const json::type_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to parse main config: " + std::string(e.what()));
	}
	catch (const json::other_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to parse main config: " + std::string(e.what()));
	}

	if (this->vSidConfig.is_null())
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to parse main config. (Critical!)");
		return;
	}

	// set topsky preference

	this->preferTopsky = this->vSidConfig.value("preferTopsky", true);

	// set update notification

	this->notifyUpdate = this->vSidConfig.value("notifyUpdate", 1);

	// set hand over warning altitude

	this->hovWarningAlt = this->vSidConfig.value("hovWarningAlt", 1500);

	// set logging of dev only messages

	this->logDevOnly = this->vSidConfig.value("logDevOnly", false);

	// optional external strategy source ("provider/field")

	this->strategySource = "";
	if (this->vSidConfig.contains("strategySource"))
	{
		if (this->vSidConfig.at("strategySource").is_string()) this->strategySource = this->vSidConfig.at("strategySource").get<std::string>();
		else vsid::Logger::log(vsid::LogLevel::Warning, "Main config: \"strategySource\" must be a \"provider/field\" string; ignored.");
	}

	try
	{
		// import colors or set default values

		if (this->vSidConfig.at("colors").contains("sidSuggestion"))
		{
			this->colors["sidSuggestion"] = RGB(
				this->vSidConfig.at("colors").at("sidSuggestion").value("r", 255),
				this->vSidConfig.at("colors").at("sidSuggestion").value("g", 255),
				this->vSidConfig.at("colors").at("sidSuggestion").value("b", 255)
			);
		}
		else this->colors["sidSuggestion"] = RGB(255, 255, 255);
		
		if (this->vSidConfig.at("colors").contains("suggestedSidSet"))
		{
			this->colors["suggestedSidSet"] = RGB(
				this->vSidConfig.at("colors").at("suggestedSidSet").value("r", 0),
				this->vSidConfig.at("colors").at("suggestedSidSet").value("g", 255),
				this->vSidConfig.at("colors").at("suggestedSidSet").value("b", 0)
			);
		}
		else this->colors["suggestedSidSet"] = RGB(0, 255, 0);

		if (this->vSidConfig.at("colors").contains("customSidSuggestion"))
		{
			this->colors["customSidSuggestion"] = RGB(
				this->vSidConfig.at("colors").at("customSidSuggestion").value("r", 255),
				this->vSidConfig.at("colors").at("customSidSuggestion").value("g", 255),
				this->vSidConfig.at("colors").at("customSidSuggestion").value("b", 180)
			);
		}
		else this->colors["customSidSuggestion"] = RGB(255, 255, 180);

		if (this->vSidConfig.at("colors").contains("customSidSet"))
		{
			this->colors["customSidSet"] = RGB(
				this->vSidConfig.at("colors").at("customSidSet").value("r", 255),
				this->vSidConfig.at("colors").at("customSidSet").value("g", 120),
				this->vSidConfig.at("colors").at("customSidSet").value("b", 30)
			);
		}
		else this->colors["customSidSet"] = RGB(255, 120, 30);

		if (this->vSidConfig.at("colors").contains("noSid"))
		{
			this->colors["noSid"] = RGB(
				this->vSidConfig.at("colors").at("noSid").value("r", 220),
				this->vSidConfig.at("colors").at("noSid").value("g", 30),
				this->vSidConfig.at("colors").at("noSid").value("b", 20)
			);
		}
		else this->colors["noSid"] = RGB(220, 30, 20);

		if (this->vSidConfig.at("colors").contains("sidHighlight"))
		{
			this->colors["sidHighlight"] = RGB(
				this->vSidConfig.at("colors").at("sidHighlight").value("r", 240),
				this->vSidConfig.at("colors").at("sidHighlight").value("g", 90),
				this->vSidConfig.at("colors").at("sidHighlight").value("b", 190)
			);
		}
		else this->colors["sidHighlight"] = RGB(240, 90, 190);

		if (this->vSidConfig.at("colors").contains("suggestedClmb"))
		{
			this->colors["suggestedClmb"] = RGB(
				this->vSidConfig.at("colors").at("suggestedClmb").value("r", 255),
				this->vSidConfig.at("colors").at("suggestedClmb").value("g", 255),
				this->vSidConfig.at("colors").at("suggestedClmb").value("b", 255)
			);
		}
		else this->colors["suggestedClmb"] = RGB(255, 255, 255);

		if (this->vSidConfig.at("colors").contains("customClmbSet"))
		{
			this->colors["customClmbSet"] = RGB(
				this->vSidConfig.at("colors").at("customClmbSet").value("r", 255),
				this->vSidConfig.at("colors").at("customClmbSet").value("g", 120),
				this->vSidConfig.at("colors").at("customClmbSet").value("b", 30)
			);
		}
		else this->colors["customClmbSet"] = RGB(255, 120, 30);

		if (this->vSidConfig.at("colors").contains("clmbSet"))
		{
			this->colors["clmbSet"] = RGB(
				this->vSidConfig.at("colors").at("clmbSet").value("r", 50),
				this->vSidConfig.at("colors").at("clmbSet").value("g", 240),
				this->vSidConfig.at("colors").at("clmbSet").value("b", 210)
			);
		}
		else this->colors["clmbSet"] = RGB(50, 240, 210);

		if (this->vSidConfig.at("colors").contains("clmbViaSet"))
		{
			this->colors["clmbViaSet"] = RGB(
				this->vSidConfig.at("colors").at("clmbViaSet").value("r", 0),
				this->vSidConfig.at("colors").at("clmbViaSet").value("g", 255),
				this->vSidConfig.at("colors").at("clmbViaSet").value("b", 0)
			);
		}
		else this->colors["clmbViaSet"] = RGB(0, 255, 0);

		if (this->vSidConfig.at("colors").contains("clmbHighlight"))
		{
			this->colors["clmbHighlight"] = RGB(
				this->vSidConfig.at("colors").at("clmbHighlight").value("r", 240),
				this->vSidConfig.at("colors").at("clmbHighlight").value("g", 90),
				this->vSidConfig.at("colors").at("clmbHighlight").value("b", 190)
			);
		}
		else this->colors["clmbHighlight"] = RGB(240, 90, 190);

		if (this->vSidConfig.at("colors").contains("rwyNotSet"))
		{
			this->colors["rwyNotSet"] = RGB(
				this->vSidConfig.at("colors").at("rwyNotSet").value("r", 255),
				this->vSidConfig.at("colors").at("rwyNotSet").value("g", 255),
				this->vSidConfig.at("colors").at("rwyNotSet").value("b", 255)
			);
		}
		else this->colors["rwyNotSet"] = RGB(255, 255, 255);

		if (this->vSidConfig.at("colors").contains("rwySet"))
		{
			this->colors["rwySet"] = RGB(
				this->vSidConfig.at("colors").at("rwySet").value("r", 0),
				this->vSidConfig.at("colors").at("rwySet").value("g", 255),
				this->vSidConfig.at("colors").at("rwySet").value("b", 0)
			);
		}
		else this->colors["rwySet"] = RGB(0, 255, 0);

		if (this->vSidConfig.at("colors").contains("notDepRwySet"))
		{
			this->colors["notDepRwySet"] = RGB(
				this->vSidConfig.at("colors").at("notDepRwySet").value("r", 230),
				this->vSidConfig.at("colors").at("notDepRwySet").value("g", 230),
				this->vSidConfig.at("colors").at("notDepRwySet").value("b", 60)
			);
		}
		else this->colors["notDepRwySet"] = RGB(230, 230, 60);

		if (this->vSidConfig.at("colors").contains("squawkSet"))
		{
			this->colors["squawkSet"] = RGB(
				this->vSidConfig.at("colors").at("squawkSet").value("r", 255),
				this->vSidConfig.at("colors").at("squawkSet").value("g", 255),
				this->vSidConfig.at("colors").at("squawkSet").value("b", 255)
			);
		}
		// no else case for squawk set due to special values following below

		if (this->vSidConfig.at("colors").contains("squawkNotSet"))
		{
			this->colors["squawkNotSet"] = RGB(
				this->vSidConfig.at("colors").at("squawkNotSet").value("r", 230),
				this->vSidConfig.at("colors").at("squawkNotSet").value("g", 230),
				this->vSidConfig.at("colors").at("squawkNotSet").value("b", 60)
			);
		}
		else this->colors["squawkNotSet"] = RGB(230, 230, 60);

		if (this->vSidConfig.at("colors").contains("requestNeutral"))
		{
			this->colors["requestNeutral"] = RGB(
				this->vSidConfig.at("colors").at("requestNeutral").value("r", 128),
				this->vSidConfig.at("colors").at("requestNeutral").value("g", 128),
				this->vSidConfig.at("colors").at("requestNeutral").value("b", 128)
			);
		}
		else this->colors["requestNeutral"] = RGB(128, 128, 128);

		if (this->vSidConfig.at("colors").contains("requestCaution"))
		{
			this->colors["requestCaution"] = RGB(
				this->vSidConfig.at("colors").at("requestCaution").value("r", 230),
				this->vSidConfig.at("colors").at("requestCaution").value("g", 230),
				this->vSidConfig.at("colors").at("requestCaution").value("b", 60)
			);
		}
		else this->colors["requestCaution"] = RGB(230, 230, 60);

		if (this->vSidConfig.at("colors").contains("requestWarning"))
		{
			this->colors["requestWarning"] = RGB(
				this->vSidConfig.at("colors").at("requestWarning").value("r", 220),
				this->vSidConfig.at("colors").at("requestWarning").value("g", 30),
				this->vSidConfig.at("colors").at("requestWarning").value("b", 20)
			);
		}
		else this->colors["requestWarning"] = RGB(220, 30, 20);

		if (this->vSidConfig.at("colors").contains("clrfSet"))
		{
			this->colors["clrfSet"] = RGB(
				this->vSidConfig.at("colors").at("clrfSet").value("r", 0),
				this->vSidConfig.at("colors").at("clrfSet").value("g", 160),
				this->vSidConfig.at("colors").at("clrfSet").value("b", 30)
			);
		}
		else this->colors["clrfSet"] = RGB(0, 160, 30);

		if (this->vSidConfig.at("colors").contains("clrfCaution"))
		{
			this->colors["clrfCaution"] = RGB(
				this->vSidConfig.at("colors").at("clrfCaution").value("r", 250),
				this->vSidConfig.at("colors").at("clrfCaution").value("g", 160),
				this->vSidConfig.at("colors").at("clrfCaution").value("b", 0)
			);
		}
		else this->colors["clrfCaution"] = RGB(250, 160, 0);

		if (this->vSidConfig.at("colors").contains("clrfWarning"))
		{
			this->colors["clrfWarning"] = RGB(
				this->vSidConfig.at("colors").at("clrfWarning").value("r", 200),
				this->vSidConfig.at("colors").at("clrfWarning").value("g", 10),
				this->vSidConfig.at("colors").at("clrfWarning").value("b", 10)
			);
		}
		else this->colors["clrfWarning"] = RGB(200, 10, 10);

		if (this->vSidConfig.at("colors").contains("intsecSet"))
		{
			this->colors["intsecSet"] = RGB(
				this->vSidConfig.at("colors").at("intsecSet").value("r", 0),
				this->vSidConfig.at("colors").at("intsecSet").value("g", 150),
				this->vSidConfig.at("colors").at("intsecSet").value("b", 50)
			);
		}
		else this->colors["intsecSet"] = RGB(0, 150, 50);

		if (this->vSidConfig.at("colors").contains("intsecAble"))
		{
			this->colors["intsecAble"] = RGB(
				this->vSidConfig.at("colors").at("intsecAble").value("r", 200),
				this->vSidConfig.at("colors").at("intsecAble").value("g", 150),
				this->vSidConfig.at("colors").at("intsecAble").value("b", 0)
			);
		}

		if (this->vSidConfig.at("colors").contains("intsecSetIndicator"))
		{
			this->colors["intsecSetIndicator"] = RGB(
				this->vSidConfig.at("colors").at("intsecSetIndicator").value("r", 0),
				this->vSidConfig.at("colors").at("intsecSetIndicator").value("g", 150),
				this->vSidConfig.at("colors").at("intsecSetIndicator").value("b", 50)
			);
		}

		if (this->vSidConfig.at("colors").contains("intsecAbleIndicator"))
		{
			this->colors["intsecAbleIndicator"] = RGB(
				this->vSidConfig.at("colors").at("intsecAbleIndicator").value("r", 200),
				this->vSidConfig.at("colors").at("intsecAbleIndicator").value("g", 150),
				this->vSidConfig.at("colors").at("intsecAbleIndicator").value("b", 0)
			);
		}

		else this->colors["intsecSetIndicator"] = RGB(200, 150, 0);

		if (this->vSidConfig.at("colors").contains("pbIndicator"))
		{
			this->colors["pbIndicator"] = RGB(
				this->vSidConfig.at("colors").at("pbIndicator").value("r", 0),
				this->vSidConfig.at("colors").at("pbIndicator").value("g", 255),
				this->vSidConfig.at("colors").at("pbIndicator").value("b", 0)
			);
		}
		else this->colors["pbIndicator"] = RGB(0, 255, 0);

		if (this->vSidConfig.at("colors").contains("reqIndicator"))
		{
			this->colors["reqIndicator"] = RGB(
				this->vSidConfig.at("colors").at("reqIndicator").value("r", 255),
				this->vSidConfig.at("colors").at("reqIndicator").value("g", 255),
				this->vSidConfig.at("colors").at("reqIndicator").value("b", 255)
			);
		}
		else this->colors["reqIndicator"] = RGB(255, 255, 255);

		if (this->vSidConfig.at("colors").contains("hovNeutral"))
		{
			this->colors["hovNeutral"] = RGB(
				this->vSidConfig.at("colors").at("hovNeutral").value("r", 250),
				this->vSidConfig.at("colors").at("hovNeutral").value("g", 160),
				this->vSidConfig.at("colors").at("hovNeutral").value("b", 0)
			);
		}
		else this->colors["hovNeutral"] = RGB(250, 160, 0);

		if (this->vSidConfig.at("colors").contains("hovWarning"))
		{
			this->colors["hovWarning"] = RGB(
				this->vSidConfig.at("colors").at("hovWarning").value("r", 250),
				this->vSidConfig.at("colors").at("hovWarning").value("g", 0),
				this->vSidConfig.at("colors").at("hovWarning").value("b", 0)
			);
		}
		else this->colors["hovWarning"] = RGB(250, 0, 0);

		// pseudo values for special color use cases
		if (!this->colors.contains("squawkSet")) this->colors["squawkSet"] = RGB(300, 300, 300);
	}
	catch (std::error_code& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to import colors: " + e.message());
	}
	catch (const json::parse_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "[Parse Error] in color config section: " + std::string(e.what()));
	}
	catch (const json::type_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "[Type Error] in color config section: " + std::string(e.what()));
	}
	catch (const json::other_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "[Other Error] in color config section: " + std::string(e.what()));
	}
	catch (const json::out_of_range& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "[Out of Range] in color config section: " + std::string(e.what()));
	}

	// get request times

	try
	{
		this->reqTimes.insert({ "caution", this->vSidConfig.at("requests").value("caution", 2) });
		this->reqTimes.insert({ "warning", this->vSidConfig.at("requests").value("warning", 5) });
	}
	catch (json::parse_error& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to get request timers: " + std::string(e.what()));
	}

	// get clrf min values

	try
	{
		this->clrf.altCaution = this->vSidConfig.at("clrf").value("altCaution", 1500);
		this->clrf.altWarning = this->vSidConfig.at("clrf").value("altWarning", 500);
		this->clrf.distCaution = this->vSidConfig.at("clrf").value("distCaution", 10.0);
		this->clrf.distWarning = this->vSidConfig.at("clrf").value("distWarning", 2.0);
	}
	catch (json::out_of_range& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to get clrf min values: " + std::string(e.what()));
	}

	// get indicator reference values

	try
	{
		this->indicator.offset = this->vSidConfig.at("display").value("indicatorOffset", 20);
		this->indicator.zoomScale = this->vSidConfig.at("display").value("indicatorZoomScale", 0.5);
		this->indicator.showBelowZoom = this->vSidConfig.at("display").value("indicatorShowBelowZoom", 600);
	}
	catch (json::out_of_range& e)
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Failed to get indicator default reference values: " + std::string(e.what()));
	}
}

std::filesystem::path vsid::ConfigParser::airportConfigDirectory() const
{
	if (!this->vSidConfig.contains("airportConfigs") || !this->vSidConfig.at("airportConfigs").is_string()) return {};
	const auto configured = this->vSidConfig.at("airportConfigs").get<std::string>();
	if (configured.empty()) return {};
	wchar_t modulePath[32768] = {};
	const auto length = GetModuleFileNameW((HINSTANCE)&__ImageBase, modulePath, 32768);
	if (length == 0 || length >= 32768) return {};
	return (std::filesystem::path(modulePath).parent_path() / std::filesystem::path(configured)).lexically_normal();
}

void vsid::ConfigParser::loadAirportConfig(std::map<std::string, vsid::Airport, vsid::utils::CICompare>& activeAirports,
										std::map<std::string, vsid::Airport::CustomRulesMap>& savedCustomRules,
										std::map<std::string, std::map<std::string, bool>>& savedSettings,
										std::map<std::string, vsid::v2::StrategyBook>& savedStrategies,
										std::map<std::string, vsid::Airport::CustomRequestMap>& savedRequests,
										std::map<std::string, vsid::Airport::CustomRwyRequestMap>& savedRwyRequests
										)
{
	const auto basePath = this->airportConfigDirectory();
	if (basePath.empty())
	{
		vsid::Logger::log(vsid::LogLevel::Error, "No config path for airports in main config");
		return;
	}

	if (!std::filesystem::exists(basePath))
	{
		vsid::Logger::log(vsid::LogLevel::Error, "No airport config folder found at: " + basePath.string());
		return;
	}

	std::vector<std::filesystem::path> files;
	std::set<std::string> aptConfig;

   /* for (const std::filesystem::path& entry : std::filesystem::recursive_directory_iterator(basePath)) // needs further #evaluate - can cause slow loading
	{
		if (!std::filesystem::is_directory(entry) && entry.extension() == ".json")
		{
			this->configPaths.insert(entry);
		}
	}*/

	for (auto &[icao, aptInfo] : activeAirports)
	{
		for (const std::filesystem::path& entry : std::filesystem::directory_iterator(basePath))
		//for (const std::filesystem::path& entry : this->configPaths)
		{
			if (!std::filesystem::is_directory(entry) && entry.extension() == ".json")
			{
				if (vsid::utils::svEqualCi(entry.filename().string(), "vSidAutoConfig.json")) continue;
				std::ifstream configFile(entry.string());

				try
				{
					this->parsedConfig = json::parse(configFile);

					if (!this->parsedConfig.contains(icao)) continue;
					else
					{
						// procedures, areas and strategies (config v2) - a rejected config leaves the airport unloaded

						vsid::v2::AirportConfig sidConfig;
						std::string error;

						if (!vsid::v2::parseAirport(this->parsedConfig.at(icao), icao, sidConfig, error))
						{
							vsid::Logger::log(vsid::LogLevel::Error, std::format("[{}] config v2 rejected: {}", icao, error));
							continue;
						}

						aptConfig.insert(icao);

						// general settings

						aptInfo.icao = icao;
						aptInfo.elevation = this->parsedConfig.at(icao).value("elevation", 0);
						aptInfo.equipCheck = this->parsedConfig.at(icao).value("equipCheck", true);
						aptInfo.enableRVSids = this->parsedConfig.at(icao).value("enableRVSids", true);
						aptInfo.allRwys = vsid::utils::split(this->parsedConfig.at(icao).value("runways", ""), ',');
						aptInfo.transAlt = this->parsedConfig.at(icao).value("transAlt", 0);
						aptInfo.maxInitialClimb = this->parsedConfig.at(icao).value("maxInitialClimb", 0);
						aptInfo.requests["clearance"] = {};
						aptInfo.requests["startup"] = {};
						aptInfo.requests["pushback"] = {};
						aptInfo.requests["taxi"] = {};
						aptInfo.requests["departure"] = {};
						aptInfo.requests["vfr"] = {};
						aptInfo.rwyrequests["startup"] = {};
						aptInfo.autoHandoff = this->parsedConfig.at(icao).value("autoHandoff", true);

						// customRules

						vsid::Airport::CustomRulesMap customRules;
						for (auto &el : this->parsedConfig.at(icao).value("customRules", std::map<std::string, bool>{}))
						{
							std::pair<std::string, bool> rule = { vsid::utils::toupper(el.first), el.second };
							customRules.insert(rule);
						}

						// overwrite loaded rule settings from config with current values at the apt

						if (savedCustomRules.contains(icao))
						{
							for (std::pair<const std::string, bool>& rule : savedCustomRules[icao])
							{
								if (customRules.contains(rule.first))
								{
									customRules[rule.first] = rule.second;
								}
							}
						}
						aptInfo.customRules = customRules;                        

						std::set<std::string> appSI;
						int appSIPrio = 0;
						for (std::string& si : vsid::utils::split(this->parsedConfig.at(icao).value("appSI", ""), ','))
						{
							aptInfo.appSI[si] = appSIPrio;
							appSIPrio++;
						}
						
						// intersections

						if (this->parsedConfig.at(icao).contains("intersections"))
						{
							for (auto& intsecList : this->parsedConfig.at(icao).at("intersections").items())
							{
								std::string rwy = intsecList.key();
								std::vector<std::string> intsec = vsid::utils::split(intsecList.value(), ',');

								aptInfo.intsec.insert({ rwy, intsec });
							}
						}

						// airport settings

						if (savedSettings.contains(icao))
						{
							aptInfo.settings = savedSettings[icao];
						}
						else
						{
							aptInfo.settings = { {"auto", false} };
						}

						// saved requests - if not found base settings already in general settings

						if (savedRequests.contains(icao)) aptInfo.requests = savedRequests[icao];

						// saved rwy requests - if not found base settings already in general settings

						if (savedRwyRequests.contains(icao)) aptInfo.rwyrequests = savedRwyRequests[icao];

						// sids - one vsid::Sid per v2 entry, number completed by the sector file

						aptInfo.sidConfig = std::move(sidConfig);
						aptInfo.sids.clear();

						for (const vsid::v2::Entry& entry : aptInfo.sidConfig.entries)
						{
							vsid::Sid newSid;
							newSid.base = entry.waypoint;
							newSid.waypoint = entry.waypoint;
							newSid.designator = entry.designator;
							newSid.id = std::to_string(entry.id);
							newSid.rwys = entry.rwys;
							newSid.climbvia = entry.climbvia;
							newSid.initialClimb = entry.initialClimb.at('J');
							aptInfo.sids.push_back(newSid);

							vsid::Logger::log(vsid::LogLevel::Debug, std::format("[{}] [{} (ID: {})] rwys: {} | initialClimb: {} | climb via: {}",
								icao, newSid.base + "_" + newSid.designator, newSid.id, vsid::utils::join(newSid.rwys, ','), newSid.initialClimb,
								newSid.climbvia ? "TRUE" : "FALSE"), vsid::DebugLevel::Conf);
						}

						// strategy state - reset to the config default, then restored if the airport was active before

						aptInfo.strategy.reset(aptInfo.sidConfig);

						if (auto saved = savedStrategies.find(icao); saved != savedStrategies.end())
						{
							const std::string& active = saved->second.active();

							if (active.empty() || aptInfo.sidConfig.findStrategy(active)) aptInfo.strategy = saved->second;
							else vsid::Logger::log(vsid::LogLevel::Warning, std::format("[{}] strategy [{}] is no longer configured. Back to the config default.",
								icao, active));
						}
					}
				}
				catch (const json::parse_error& e)
				{
					vsid::Logger::log(vsid::LogLevel::Error, std::format("[Parse] Failed to load airport config ({}): {}", icao, e.what()));
				}
				catch (const json::type_error& e)
				{
					vsid::Logger::log(vsid::LogLevel::Error, std::format("[Type] Failed to load airport config ({}): {}", icao, e.what()));
				}
				catch (const json::out_of_range& e)
				{
					vsid::Logger::log(vsid::LogLevel::Error, std::format("[Range] Failed to load airport config ({}): {}", icao, e.what()));
				}
				catch (const json::other_error& e)
				{
					vsid::Logger::log(vsid::LogLevel::Error, std::format("[Other] Failed to load airport config ({}): {}", icao, e.what()));
				}
				catch (const std::exception &e)
				{
					vsid::Logger::log(vsid::LogLevel::Error, std::format("Failure in config ({}): {}", icao, e.what()));
				}

				/* DOCUMENTATION on how to get all values below a key
				json waypoint = this->configFile.at("EDDF").at("sids").at("MARUN");
				for (auto it : waypoint.items())
				{
					vsid::messagehandler::LogMessage("JSON it:", it.value().dump());
				}*/
			}
		}
	}

	// airport health check - remove apt without config

	for (std::map<std::string, vsid::Airport>::iterator it = activeAirports.begin(); it != activeAirports.end();)
	{
		if (aptConfig.contains(it->first)) ++it;
		else
		{
			vsid::Logger::log(vsid::LogLevel::Info, "No config found for: " + it->first);
			it = activeAirports.erase(it);
		}
	}
}

void vsid::ConfigParser::loadGrpConfig()
{
	// get the current path where plugins .dll is stored
	char path[MAX_PATH + 1] = { 0 };
	GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
	PathRemoveFileSpecA(path);
	std::filesystem::path basePath = path;

	if (!this->vSidConfig.empty())
	{
		basePath.append(this->vSidConfig.value("grp", "")).make_preferred();
	}

	if (!std::filesystem::exists(basePath))
	{
		vsid::Logger::log(vsid::LogLevel::Error, "No grp config found in: " + basePath.string());
		return;
	}
	for (const std::filesystem::path& entry : std::filesystem::directory_iterator(basePath))
	{
		if (entry.extension() == ".json")
		{
			if (entry.filename().string() != "ICAO_Aircraft.json") continue;

			std::ifstream configFile(entry.string());

			try
			{
				this->grpConfig = json::parse(configFile);
			}
			catch (const json::parse_error& e)
			{
				vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to load grp config: {}", e.what()));
			}
			catch (const json::type_error& e)
			{
				vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to load grp config: {}", e.what()));
			}
		}
	}
}

void vsid::ConfigParser::loadRnavList()
{
	// get the current path where plugins .dll is stored
	char path[MAX_PATH + 1] = { 0 };
	GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
	PathRemoveFileSpecA(path);
	std::filesystem::path basePath = path;

	if (!this->vSidConfig.empty())
	{
		basePath.append(this->vSidConfig.value("RNAV", "")).make_preferred();
	}

	if (!std::filesystem::exists(basePath))
	{
		vsid::Logger::log(vsid::LogLevel::Error, "Path to check for RNAV List does not exist: " + basePath.string());
		return;
	}

	for (const std::filesystem::path& entry : std::filesystem::directory_iterator(basePath))
	{
		if (entry.extension() != ".json") continue;
		if (entry.filename().string() != "RNAV_List.json") continue;

		std::ifstream configFile(entry.string());
		json rnavConfigFile;

		try
		{
			rnavConfigFile = json::parse(configFile);
		}
		catch (const json::parse_error& e)
		{
			vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to load rnav list: {}", e.what()));
		}
		catch (const json::type_error& e)
		{
			vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to load rnav list: {}", e.what()));
		}
		
		if (rnavConfigFile.empty())
		{
			vsid::Logger::log(vsid::LogLevel::Error, "RNAV List is empty. Is it present besides the plugin DLL file?");
			return;
		}

		try
		{
			this->rnavList = rnavConfigFile.value("RNAV", std::set<std::string>{});
		}
		catch (json::type_error &e)
		{
			vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to read rnav list: {}", e.what()));
		}

		return;
	}
	vsid::Logger::log(vsid::LogLevel::Error, "No RNAV capable list found at: " + basePath.string());
}

const COLORREF vsid::ConfigParser::getColor(std::string color)
{
	if (this->colors.contains(color))
	{
		messageHandler->removeGenError(ERROR_CONF_COLOR + "_" + color);

		return this->colors[color];
	}
	else
	{
		if (!messageHandler->genErrorsContains(ERROR_CONF_COLOR + "_" + color))
		{
			vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to retrieve color: [{}]. Code: {}", color, ERROR_CONF_COLOR));
			messageHandler->addGenError(ERROR_CONF_COLOR + "_" + color);
		}
		// return purple if color could not be found to signal error
		COLORREF rgbColor = RGB(190, 30, 190);
		return rgbColor;
	}
}

int vsid::ConfigParser::getReqTime(std::string time)
{
	if (this->reqTimes.contains(time))
	{
		return this->reqTimes[time];
	}
	else
	{
		vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to retrieve request time setting for key [{}]", time));
		return 0;
	}
}
