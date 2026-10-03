#pragma once

#include <map>

namespace vsid::configurationrefresh
{
	/** @brief Save and invalidate uncleared automode flights before refreshing suggestions.
	 * Selection and callbacks allow the same cache lifecycle to be tested without EuroScope.
	 */
	template<typename Processed, typename Select, typename Affected, typename Automatic, typename Save, typename Recheck>
	void refresh(Processed& processed, Select select, Affected affected, Automatic automatic, Save save, Recheck recheck)
	{
		std::erase_if(processed, [&](const auto& entry)
			{
				const auto& [callsign, info] = entry;
				auto flight = select(callsign);
				if (!flight.IsValid() || flight.GetClearenceFlag()) return false;
				const auto origin = flight.GetFlightPlanData().GetOrigin();
				if (!affected(origin) || !automatic(origin)) return false;
				save(callsign, info);
				return true;
			});
		for (const auto& [callsign, info] : processed)
		{
			auto flight = select(callsign);
			if (flight.IsValid() && affected(flight.GetFlightPlanData().GetOrigin())) recheck(flight);
		}
	}
}
