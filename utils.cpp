#include "pch.h"
#include "utils.h"

#include <sstream>
#include <algorithm>

std::string vsid::utils::ltrim(const std::string& string)
{
	std::string string_to_trim = string;
	string_to_trim.erase(string.find_last_not_of(' ') + 1);
	return string_to_trim;
}

std::string vsid::utils::rtrim(const std::string& string)
{
	std::string string_to_trim = string;
	string_to_trim.erase(0, string.find_first_not_of(' '));
	return string_to_trim;
}

std::string vsid::utils::trim(const std::string& string)
{
	return vsid::utils::ltrim(vsid::utils::rtrim(string));
}

std::vector<std::string> vsid::utils::split(const std::string &string, const char &del, const bool keepWhitespace)
{
	std::istringstream ss(string);
	std::vector<std::string> elems;
	std::string elem;

	while (std::getline(ss, elem, del))
	{
		if (elem == "" && !keepWhitespace) continue; // remove excessive whitespaces to prevent a crash caused by wrong routes
		elems.push_back(vsid::utils::trim(elem));
	}
	return elems;
}

std::string vsid::utils::join(const std::vector<std::string>& toJoin, const char del)
{
	if (toJoin.empty()) return "";
	std::ostringstream ss;
	for (const auto& elem : toJoin) // possible improvement back to a copy function
	{
		ss << elem << del;
	}
	std::string joinedStr = ss.str();
	return joinedStr.erase(joinedStr.length() - 1, 1);
}

std::string vsid::utils::join(const std::set<std::string>& toJoin, char del)
{
	if (toJoin.empty()) return "";
	std::ostringstream ss;
	for (const auto& elem : toJoin) // possible improvement back to a copy function
	{
		ss << elem << del;
	}
	std::string joinedStr = ss.str();
	return joinedStr.erase(joinedStr.length() - 1, 1);
}

std::vector<std::string> vsid::utils::splitRoute(std::string& string)
{
	std::vector<std::string> elems;
	std::string elem;
	size_t pos = 0;

	string = vsid::utils::trim(string);

	while ((pos = string.find(' ')) != std::string::npos)
	{
		elem = string.substr(0, pos);
		if (elem.find('/') != std::string::npos)
		{
			elem = vsid::utils::split(elem, '/').at(0);
		}
		elems.push_back(elem);
		string.erase(0, pos + 1);
		vsid::utils::ltrim(string);
	}

	elems.push_back(string);

	return elems;
}

bool vsid::utils::isIcaoInVector(const std::vector<vsid::Airport>& airportVector, const std::string& toSearch)
{
	for (const vsid::Airport &elem : airportVector)
	{
		if (elem.icao == toSearch)
		{
			return true;
		}
	}
	return false;
}

bool vsid::utils::containsDigit(int number, int digit)
{
	if (!number)
	{
		return true;
	}
	while (number)
	{
		int currDigit = number % 10;
		if (currDigit == digit)
		{
			return true;
		}
		number /= 10;
	}
	return false;
}

int vsid::utils::getMinClimb(int elevation)
{
	return int (std::ceil((float)elevation / 1000) * 1000) + 500;
}

std::string vsid::utils::tolower(std::string input)
{
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
	return input;
}

std::string vsid::utils::toupper(std::string input)
{
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::toupper(c); });
	return input;
}

EuroScopePlugIn::CPosition vsid::utils::toPoint(const std::pair<std::string, std::string>& pos)
{
	EuroScopePlugIn::CPosition p;

	// Handle case where both latitude and longitude are provided in a single string separated by ':',
	// or if pos.second is empty and pos.first contains both latitude and longitude
	if ((pos.second.empty() || pos.second.find_first_not_of(' ') == std::string::npos) && pos.first.find(':') != std::string::npos)
	{
		std::vector<std::string> parts = vsid::utils::split(pos.first, ':');
		if (parts.size() >= 2)
		{
			// determine which part is lat and which is lon based on directional letters
			std::string a = vsid::utils::trim(parts[0]);
			std::string b = vsid::utils::trim(parts[1]);
			if (!a.empty() && (a.front() == 'N' || a.front() == 'S'))
			{
				p.m_Latitude = toDeg(a);
				p.m_Longitude = toDeg(b);
			}
			else if (!b.empty() && (b.front() == 'N' || b.front() == 'S'))
			{
				p.m_Latitude = toDeg(b);
				p.m_Longitude = toDeg(a);
			}
			else
			{
				// fallback: first -> latitude, second -> longitude
				p.m_Latitude = toDeg(a);
				p.m_Longitude = toDeg(b);
			}

			return p;
		}
	}

	p.m_Latitude = toDeg(pos.first);
	p.m_Longitude = toDeg(pos.second);

	return p;
}

double vsid::utils::toDeg(const std::string& coord)
{
	std::string c = vsid::utils::trim(coord);

	if (c.empty())
	{
		messageHandler->writeMessage("WARNING", "Empty coordinate string found! Skipping coordinate.");
		return 0.0;
	}

	// If someone passed a combined lat:lon string, try to extract a single directional part
	if (c.find(':') != std::string::npos)
	{
		std::vector<std::string> parts = vsid::utils::split(c, ':');
		for (const auto& part : parts)
		{
			if (!part.empty() && (part.front() == 'N' || part.front() == 'S' || part.front() == 'E' || part.front() == 'W'))
			{
				c = part;
				break;
			}
		}
	}

	std::vector<std::string> dms = vsid::utils::split(c, '.');
	int multi = 1; // assume positive unless we detect S or W

	try
	{
		if (dms.empty())
			throw std::out_of_range("Coordinate string \"" + c + "\" does not contain enough parts for DMS conversion!");

		// determine sign from first part
		if (dms.at(0).find('S') != std::string::npos || dms.at(0).find('W') != std::string::npos)
			multi = -1;

		// degrees: expect first part to start with directional letter followed by degrees or just degrees
		std::string degPart = dms[0];
		if (!degPart.empty() && (degPart.front() == 'N' || degPart.front() == 'S' || degPart.front() == 'E' || degPart.front() == 'W'))
			degPart = degPart.substr(1);

		double deg = 0.0;
		double min = 0.0;
		double sec = 0.0;

		if (!degPart.empty())
			deg = std::stod(degPart);

		if (dms.size() >= 2 && !dms[1].empty())
			min = std::stod(dms[1]) / 60.0;

		if (dms.size() >= 3 && !dms[2].empty())
			sec = std::stod(dms[2]) / 3600.0;

		// if there's a fractional seconds part (4th part), add it as decimal fraction
		if (dms.size() >= 4 && !dms[3].empty())
		{
			double frac = std::stod(std::string("0.") + dms[3]);
			sec += frac / 3600.0;
		}

		return (deg + min + sec) * multi;
	}
	catch (std::out_of_range& e)
	{
		messageHandler->writeMessage("ERROR", "Out of bounds while calculating coordinate: " + c + ". " + e.what());
	}
	catch (const std::invalid_argument& e)
	{
		messageHandler->writeMessage("ERROR", "Invalid number format in coord " + c + ". " + e.what());
	}

	messageHandler->writeMessage("WARNING", "Fallback state for \"" + c + "\"! Failed to calculate. DMS will be set to 0.0");

	return 0.0;
}
