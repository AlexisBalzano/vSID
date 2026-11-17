/*
vSID is a plugin for the Euroscope controller software on the Vatsim network.
The aim auf vSID is to ease the work of any controller that edits and assigns
SIDs to flightplans.

Copyright (C) 2024 Gameagle (Philip Maier)
Repo @ https://github.com/Gameagle/vSID

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>
#include <chrono>
#include <format>
#include <unordered_map>

#include "constants.h"
#include "utils.h"
#include "logger.h"
#include "messageHandler.h"

namespace vsid
{
	namespace time
	{
		//************************************
		// Description: Get the current date as string in format YYYY-MM-DD
		// Method:    getDate
		// FullName:  vsid::time::getDate
		// Access:    public 
		// Returns:   std::string
		// Qualifier:
		//************************************
		inline std::string getDate()
		{
			return std::format("{:%Y-%m-%d}", std::chrono::system_clock::now());
		}

		//************************************
		// Description: Special workaround function to use legacy code for Wine or where modern C++ isn't available
		// Method:    getFormattedTime
		// FullName:  vsid::time::getFormattedTime
		// Access:    public 
		// Returns:   std::string
		// Qualifier:
		// Parameter: const std::chrono::time_point<T
		// Parameter: U> & tp
		// Parameter: std::string_view fmtStr
		//************************************
		template<typename T, typename U>
		std::string getFormattedTime(const std::chrono::time_point<T, U>& tp, std::string_view fmtStr = "%Y-%m-%d %H:%M:%S")
		{
			auto sysTp = std::chrono::clock_cast<std::chrono::system_clock>(tp);

			std::time_t time = std::chrono::system_clock::to_time_t(sysTp);
			std::tm tm{};
			gmtime_s(&tm, &time);

			std::array<char, 64> timeBuffer;

			if (std::strftime(timeBuffer.data(), timeBuffer.size(), fmtStr.data(), &tm) > 0) return std::string(timeBuffer.data());

			return "TIME_ERROR";
		}

		/**
		 * @brief Transform a timepoint into a string
		 * 
		 * @tparam T - the clock e.g. utc_clock
		 * @tparam U - time resolutiong e.g. ::seconds
		 * @param timePoint 
		 * 
		 */
		template<typename T, typename U>
		std::string toFullString(const std::chrono::time_point<T, U>& timePoint)
		{
			return std::string(std::format("{:%Y.%m.%d %H:%M:%S}", timePoint));
		}

		//************************************
		// Description: Transform a timepoint into a time string
		// Method:    toTimeString
		// FullName:  vsid::time::toTimeString
		// Access:    public 
		// Returns:   std::string
		// Qualifier:
		// Parameter: const std::chrono::time_point<T
		// Parameter: U> & timePoint
		//************************************
		template<typename T, typename U>
		std::string toTimeString(const std::chrono::time_point<T, U>& timePoint)
		{
			if (vsid::utils::usingWine())
			{
				return vsid::time::getFormattedTime(timePoint, "%H:%M:%S");
			}

			return std::string(std::format("{:%H:%M:%S}", timePoint));
		}
	}
}


