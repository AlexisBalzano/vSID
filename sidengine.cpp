#include "sidengine.h"
#include <algorithm>

namespace vsid::v2
{
	namespace
	{
		std::vector<std::string> splitCsv(const std::string& text)
		{
			std::vector<std::string> out;
			std::string cur;
			for (char c : text)
			{
				if (c == ',') { out.push_back(cur); cur.clear(); }
				else cur += c;
			}
			out.push_back(cur);
			return out;
		}

		bool arrActive(const Request& req, const std::string& rwy) { return req.arrRwys.contains(rwy); }

		bool actArrRwyOk(const RwyCondition& c, const Request& req)
		{
			auto active = [&](const std::string& rwy) { return arrActive(req, rwy); };
			if (!c.allowAll.empty())
			{
				const auto rwys = splitCsv(c.allowAll);
				if (!std::all_of(rwys.begin(), rwys.end(), active)) return false;
			}
			else if (!c.allowAny.empty())
			{
				const auto rwys = splitCsv(c.allowAny);
				if (std::none_of(rwys.begin(), rwys.end(), active)) return false;
			}
			if (!c.denyAll.empty())
			{
				const auto rwys = splitCsv(c.denyAll);
				if (std::all_of(rwys.begin(), rwys.end(), active)) return false;
			}
			else if (!c.denyAny.empty())
			{
				const auto rwys = splitCsv(c.denyAny);
				if (std::any_of(rwys.begin(), rwys.end(), active)) return false;
			}
			return true;
		}

		bool equipOk(const Entry& e, const Request& req)
		{
			if (!req.equipCheck) return true;
			const std::string& equip = req.equipment;
			if (e.equip.contains("RNAV"))
			{
				if (equip.size() > 1 && equip.find_first_of("ABGRI") == std::string::npos && req.pbn.empty()) return false;
			}
			else if (e.equip.contains("NON-RNAV"))
			{
				if (equip.size() < 2 && req.pbn.empty()) return false;
				if (equip.find_first_of("GRI") != std::string::npos || !req.pbn.empty()) return false;
			}
			else
			{
				for (const auto& [code, required] : e.equip)
				{
					const bool found = equip.find(code) != std::string::npos;
					if (required != found) return false;
				}
			}
			return true;
		}

		bool destOk(const Entry& e, const Request& req)
		{
			if (e.dest.empty()) return true;
			const auto it = e.dest.find(req.dest);
			if (it != e.dest.end()) return it->second;
			return !std::any_of(e.dest.begin(), e.dest.end(), [](const auto& d) { return d.second; });
		}

		bool ruleOk(const Entry& e, const Request& req)
		{
			if (e.rules.empty()) return true;
			return std::any_of(e.rules.begin(), e.rules.end(), [&](const std::string& r)
				{
					const auto it = req.rules.find(r);
					return it != req.rules.end() && it->second;
				});
		}

		// an entry with areas is only eligible in mode "nearest", from inside one of them
		bool areaOk(const Entry& e, const AirportConfig& cfg, const Request& req)
		{
			if (e.areas.empty()) return true;
			if (req.mode != "nearest" || !req.position) return false;
			return std::any_of(cfg.areas.begin(), cfg.areas.end(), [&](const Area& area)
				{
					return std::find(e.areas.begin(), e.areas.end(), area.name) != e.areas.end() && insidePolygon(area.points, *req.position);
				});
		}

		// the runway the entry would depart from, "" if none is usable
		std::string usableRwy(const Entry& e, const Request& req)
		{
			for (const auto& rwy : e.rwys)
			{
				if (!req.atcRwy.empty()) { if (rwy == req.atcRwy) return rwy; }
				else if (req.depRwys.contains(rwy)) return rwy;
			}
			return "";
		}

		// group order of a waypoint for a mode that is not "nearest"
		std::vector<std::string> groupOrder(const AirportConfig& cfg, const std::string& waypoint, const std::string& mode)
		{
			std::vector<std::string> order;
			const auto sel = cfg.selection.find(waypoint);
			if (sel != cfg.selection.end()) order = sel->second;
			if (mode != "default" && cfg.runwayGroups.contains(mode))
			{
				order.erase(std::remove(order.begin(), order.end(), mode), order.end());
				order.insert(order.begin(), mode);
			}
			return order;
		}
	}

	bool insidePolygon(const std::vector<GeoPoint>& polygon, const GeoPoint& p)
	{
		bool inside = false;
		const size_t n = polygon.size();
		for (size_t i = 0; i < n; ++i)
		{
			const GeoPoint& a = polygon[i];
			const GeoPoint& b = polygon[(i + 1) % n];
			if (p.lat > std::min(a.lat, b.lat) && p.lat <= std::max(a.lat, b.lat) && p.lon <= std::max(a.lon, b.lon))
			{
				const double xInter = (p.lat - a.lat) * (b.lon - a.lon) / (b.lat - a.lat) + a.lon;
				if (a.lon == b.lon || p.lon <= xInter) inside = !inside;
			}
		}
		return inside;
	}

	std::optional<std::string> areaGroupAt(const AirportConfig& cfg, const GeoPoint& p)
	{
		std::optional<std::string> found;
		for (const auto& area : cfg.areas)
		{
			if (!insidePolygon(area.points, p)) continue;
			if (area.group == "default") return std::string("default");
			if (!found) found = area.group;
		}
		return found;
	}

	Result select(const AirportConfig& cfg, const Request& req)
	{
		Result result;

		// "LSAT" is the v1 default wingType: unknown aircraft is never skipped
		if (req.aircraftKind != '?' && std::string("LSAT").find(req.aircraftKind) == std::string::npos) return result;

		std::string mode = req.mode;
		if (mode == "nearest")
		{
			mode = "default";
			if (req.position)
			{
				const auto group = areaGroupAt(cfg, *req.position);
				if (group) mode = *group;
			}
		}

		struct Candidate { const Entry* entry; std::string rwy; };
		std::map<std::string, std::vector<Candidate>> byGroup; // "" = no runway groups, or a runway in no group
		bool equipRejected = false;
		std::map<std::string, bool> equipRejectedIn;

		for (const auto& e : cfg.entries)
		{
			if (e.waypoint != req.waypoint || !req.available.contains(e.id)) continue;
			const std::string rwy = usableRwy(e, req);
			if (rwy.empty()) continue;
			if (!e.engines.empty() && e.engines.find(req.engine) == std::string::npos) continue;
			if (!e.wtc.empty() && (req.wtc.empty() || e.wtc.find(req.wtc) == std::string::npos)) continue;
			if (!destOk(e, req) || !actArrRwyOk(e.actArrRwy, req) || !ruleOk(e, req) || !areaOk(e, cfg, req)) continue;

			const std::string group = cfg.runwayGroups.empty() ? "" : cfg.groupOf(rwy);
			if (!equipOk(e, req)) { equipRejectedIn[group] = true; continue; }
			byGroup[group].push_back({ &e, rwy });
		}

		// candidate lists tried in turn, the first non-empty one gives the SID
		std::vector<std::vector<Candidate>> tiers;
		if (cfg.runwayGroups.empty())
		{
			tiers.push_back(byGroup[""]);
			equipRejected = !equipRejectedIn.empty();
		}
		else if (!cfg.selection.contains(req.waypoint))
		{
			// no selection for this waypoint: declaration order, the entries of the mode's group first
			const bool groupMode = mode != "default" && cfg.runwayGroups.contains(mode);
			std::vector<Candidate> first, rest;
			for (auto& [g, list] : byGroup)
			{
				auto& target = groupMode && g == mode ? first : rest;
				target.insert(target.end(), list.begin(), list.end());
			}
			auto byId = [](const Candidate& a, const Candidate& b) { return a.entry->id < b.entry->id; };
			std::sort(first.begin(), first.end(), byId);
			std::sort(rest.begin(), rest.end(), byId);
			tiers.push_back(std::move(first));
			tiers.push_back(std::move(rest));
			equipRejected = !equipRejectedIn.empty();
		}
		else
		{
			for (const auto& g : groupOrder(cfg, req.waypoint, mode))
			{
				tiers.push_back(byGroup[g]);
				if (equipRejectedIn.contains(g)) equipRejected = true;
			}
		}

		for (const auto& tier : tiers)
		{
			if (tier.empty()) continue;
			auto list = tier;
			std::stable_partition(list.begin(), list.end(), [](const Candidate& c) { return !c.entry->rules.empty(); });
			const Candidate& best = list.front();
			result.entryId = best.entry->id;
			result.rwy = best.rwy;
			const char key = (req.engine == 'J' || req.engine == 'P' || req.engine == 'T') ? req.engine : 'J';
			const auto climb = best.entry->initialClimb.find(key);
			result.initialClimb = climb != best.entry->initialClimb.end() ? climb->second : 0;
			result.climbvia = best.entry->climbvia;
			return result;
		}

		result.equipFailure = equipRejected;
		return result;
	}
}
