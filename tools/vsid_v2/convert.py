"""v1 -> v2 airport converter (spec §6 step 2).

Usage: python3 -m vsid_v2.convert <v1 dir> <v2 dir>   (one file per airport, report on stdout)
"""

import json
import os
import sys
from collections import Counter

from . import lfpg_extras
from .v1model import load_v1, split_csv

ENGINES = "JPT"

# airport keys read by ConfigParser::loadAirportConfig; any other top level key is dropped
V1_AIRPORT_KEYS = {"elevation", "equipCheck", "enableRVSids", "runways", "transAlt", "maxInitialClimb", "timezone",
				"autoHandoff", "customRules", "appSI", "areas", "intersections", "sids", "timeMode"}

OTHER_GROUP = "other"
NEAREST = {"nearest": {"label": "Nearest", "assign": {"*": "nearest"}}}


def _canon(value):
	return json.dumps(value, sort_keys=True)


def _clean_act_arr(act_arr):
	out = {}
	for kind in ("allow", "deny"):
		part = {k: v for k, v in act_arr.get(kind, {}).items() if v}
		if part:
			out[kind] = part
	return out


def _engines(v):
	return set(v.engine_type or ENGINES) & set(ENGINES)


def _opposite(a, b):
	na, nb = a.rstrip("LCR"), b.rstrip("LCR")
	return na.isdigit() and nb.isdigit() and abs(int(na) - int(nb)) == 18


class _Entry:
	def __init__(self, first):
		self.waypoint = first.waypoint
		self.designator = first.designator
		self.rwys = list(first.rwys)
		self.climbvia = first.climbvia
		self.equip = dict(first.equip)
		self.wtc = first.wtc
		self.dest = dict(first.dest)
		self.act_arr = _clean_act_arr(first.act_arr)
		self.variants = []

	@property
	def plain(self):
		return [v for v in self.variants if not v.area]

	@staticmethod
	def _key(variants):
		# v1 keeps the lowest prio and, at equal prio, the variant it reads last
		prio = min(v.prio for v in variants)
		last = max(v.order for v in variants if v.prio == prio)
		return prio, -last

	def sort_key(self):
		"""Rank outside the areas (area variants only count for an area-only entry)."""
		return self._key(self.plain or self.variants)

	def area_key(self):
		"""Rank inside an area, where the area variants and the plain ones compete."""
		return self._key(self.variants)

	def label(self):
		return f"{self.waypoint} {self.designator} ({','.join(self.rwys)})"


def _order(entries):
	"""v1 choice order. An area-only entry only competes inside its area: it goes before the first
	entry it beats there."""
	ordered = sorted((e for e in entries if e.plain), key=_Entry.sort_key)
	for x in sorted((e for e in entries if not e.plain), key=_Entry.area_key):
		pos = next((i for i, y in enumerate(ordered) if y.area_key() > x.area_key()), len(ordered))
		ordered.insert(pos, x)
	return ordered


def _merge_key(v):
	return (v.waypoint, v.designator, tuple(v.rwys), v.climbvia, _canon(v.equip), v.wtc, _canon(v.dest),
		_canon(_clean_act_arr(v.act_arr)), bool(v.rule))


def _most_common(values):
	return Counter(values).most_common(1)[0][0] if values else None


def convert(ap):
	"""Returns (v2 airport object, report lines)."""
	icao = ap.icao
	report = []
	notes = []

	def note(message):
		notes.append(f"{icao}: {message}")

	for anomaly in ap.anomalies:
		note(anomaly)

	runways = split_csv(ap.data.get("runways", ""))
	rule_names = {name.upper(): name for name in ap.rules}
	area_names = {name.upper(): name for name in ap.areas}

	# variants v1 can never choose do not become entries
	kept = []
	for v in ap.variants:
		where = f"{v.waypoint} {v.name}"
		if not v.designator:
			note(f"variant without designator dropped: {where}")
		elif v.prio == 99:
			note(f"prio 99 variant dropped (never chosen by v1): {where}")
		elif v.prio == 0:
			note(f"prio 0 variant dropped (forced manual selection not supported): {where}")
		elif v.rule and v.rule not in rule_names:
			note(f"variant with unknown rule {v.rule} dropped: {where}")
		elif v.area and not any(a in area_names for a in split_csv(v.area)):
			note(f"variant with unknown area {v.area} dropped (never chosen by v1): {where}")
		elif v.engine_type and not _engines(v):
			note(f"variant with engine type {v.engine_type} dropped (no J, P or T): {where}")
		elif runways and any(r not in runways for r in v.rwys):
			note(f"variant with runway outside runways dropped: {where} ({','.join(v.rwys)})")
		else:
			kept.append(v)

	# entries
	by_point = {}
	index = {}
	for v in kept:
		key = _merge_key(v)
		if key not in index:
			index[key] = _Entry(v)
			by_point.setdefault(v.waypoint, []).append(index[key])
		index[key].variants.append(v)
	for waypoint in by_point:
		by_point[waypoint] = _order(by_point[waypoint])

	for entries in by_point.values():
		for e in entries:
			e.engines = set().union(*(_engines(v) for v in e.variants))
			e.rules = sorted({rule_names[v.rule] for v in e.variants if v.rule})
			e.climb = {}
			for eng in ENGINES:
				covering = [v for v in sorted(e.variants, key=lambda v: (bool(v.area), v.order)) if eng in _engines(v)]
				if covering:
					e.climb[eng] = covering[0].initial
					if len({v.initial for v in covering}) > 1:
						note(f"initial climb conflict for {eng}: {e.label()}, kept {covering[0].initial}")
			plain = e.plain
			e.areas = []
			if not plain:
				# v1 has this entry only as area variants: v2 keeps the condition (mode nearest, inside the area)
				for v in e.variants:
					for a in split_csv(v.area):
						if a not in area_names:
							note(f"unknown area {a} of {e.waypoint} {v.name} ignored (v1 never matches it)")
						elif area_names[a] not in e.areas:
							e.areas.append(area_names[a])
				note(f"area-only entry: {e.waypoint} {'/'.join(v.name for v in e.variants)} ({','.join(e.rwys)}) "
					f"only applies inside area {','.join(e.areas)}, kept as an area entry in v2")
			else:
				if set().union(*(_engines(v) for v in plain)) != e.engines:
					note(f"engines only allowed inside an area in v1, everywhere in v2: {e.label()}")
				inside = set().union(*(_engines(v) for v in e.variants if v.area))
				if inside and inside != e.engines:
					names = "".join(eng for eng in ENGINES if eng in inside)
					note(f"area variants only for engines {names}: {e.label()}; in v1 the other engine types get no area "
						f"variant inside the area, in v2 nearest they get this entry")
				if len({v.prio for v in plain}) > 1:
					note(f"prio spread inside one entry: {e.label()} {sorted({v.prio for v in plain})}")
			if e.rules:
				pairs = {(eng, rule_names[v.rule]) for v in e.variants for eng in _engines(v)}
				if len(pairs) != len(e.engines) * len(e.rules):
					note(f"merged rules widen the entry (engine/rule pairs not all configured): {e.label()}")
			if any(c not in ENGINES for v in e.variants for c in v.engine_type):
				note(f"engine types other than J, P, T ignored: {e.label()}")

	for entries in by_point.values():
		split = {}
		for e in entries:
			split.setdefault((e.designator, tuple(e.rwys), tuple(e.rules)), []).append(e)
		for group in split.values():
			if len({e.climbvia for e in group}) > 1:
				via = "".join(eng for eng in ENGINES if any(eng in e.engines for e in group if e.climbvia))
				note(f"climbvia differs by engine type: {group[0].label()} climbvia only for {via}, kept as "
					f"{len(group)} entries")

	# runway groups: only for airports with v1 areas in use
	used_areas = [name for name in ap.areas
		if any(name.upper() in split_csv(v.area) for v in kept)]
	for name in ap.areas:
		if name not in used_areas:
			note(f"area {name} used by no variant, dropped")
	groups = {}
	if used_areas:
		all_rwys = list(runways) + sorted({r for v in kept for r in v.rwys if r not in runways})
		owner = {}
		for name in used_areas:
			rwys = {r for v in kept if name.upper() in split_csv(v.area) for r in v.rwys}
			groups[name] = []
			for r in all_rwys:
				if r not in rwys:
					continue
				if r in owner:
					note(f"runway {r} in areas {owner[r]} and {name}, kept in group {owner[r]}")
					continue
				owner[r] = name
				groups[name].append(r)
		rest = [r for r in all_rwys if r not in owner]
		if rest:
			groups[OTHER_GROUP] = rest
	group_of = {r: g for g, rwys in groups.items() for r in rwys}

	selection = {}
	if groups:
		for waypoint, entries in by_point.items():
			# groups ranked by their best entry without rule nor area, the others after
			ranks = {}
			for e in entries:
				rank = (0 if e.plain and not e.rules else 1, *e.sort_key())
				for g in {group_of[r] for r in e.rwys}:
					ranks[g] = min(ranks.get(g, rank), rank)
			selection[waypoint] = sorted(ranks, key=lambda g: ranks[g])

	_report_ties(by_point, group_of, selection, note)

	# defaults
	all_entries = [e for entries in by_point.values() for e in entries]
	equip_default = json.loads(_most_common([_canon(e.equip) for e in all_entries]) or '{"RNAV": true}')
	climb_default = {}
	for eng in ENGINES:
		value = _most_common([e.climb[eng] for e in all_entries if eng in e.climb])
		if value is not None:
			climb_default[eng] = value
	defaults = {"equip": equip_default, "initialClimb": climb_default}

	procedures = {}
	for waypoint, entries in by_point.items():
		local = {}
		if len(entries) > 1:
			equips = {_canon(e.equip) for e in entries}
			if len(equips) == 1 and entries[0].equip != equip_default:
				local["equip"] = entries[0].equip
			climb = {}
			for eng in ENGINES:
				values = {e.climb[eng] for e in entries if eng in e.climb}
				if len(values) == 1 and values != {climb_default.get(eng)}:
					climb[eng] = values.pop()
			if climb:
				local["initialClimb"] = climb
		eff_equip = local.get("equip", equip_default)
		eff_climb = dict(climb_default, **local.get("initialClimb", {}))
		emitted = [_emit(e, eff_equip, eff_climb) for e in entries]
		procedures[waypoint] = {"defaults": local, "entries": emitted} if local else emitted

	# areas and strategies
	areas = {name: {"group": name, "points": ap.areas[name]["points"]} for name in used_areas}
	strategies = {}
	default_strategy = ""
	if icao.upper() == "LFPG":
		for sid, body in lfpg_extras.STRATEGIES.items():
			assign = {}
			for key, mode in body["assign"].items():
				if key != "*" and key not in procedures:
					note(f"strategy {sid}: waypoint {key} not in the config, dropped")
				elif mode not in ("nearest", "default") and mode not in groups:
					note(f"strategy {sid}: group {mode} not in the config, dropped")
				else:
					assign[key] = mode
			strategies[sid] = {"label": body["label"], "assign": assign}
		default_strategy = lfpg_extras.DEFAULT_STRATEGY
	elif groups:
		strategies = json.loads(json.dumps(NEAREST))
		if all(ap.areas[name]["active"] for name in used_areas):
			default_strategy = "nearest"

	out = {"version": 2}
	for key, value in ap.data.items():
		if key in ("sids", "areas", "version"):
			continue
		if key not in V1_AIRPORT_KEYS:
			note(f"dropped key: {key} (not read by v1)")
			continue
		out[key] = value
	if groups:
		out["runwayGroups"] = groups
	out["defaults"] = defaults
	if areas:
		out["areas"] = areas
	out["procedures"] = procedures
	if selection:
		out["selection"] = selection
	if default_strategy:
		out["defaultStrategy"] = default_strategy
	if strategies:
		out["strategies"] = strategies

	report.append(f"{icao}: {len(ap.variants)} variants -> {len(all_entries)} entries")
	report.extend(notes)
	return out, report


def _emit(e, eff_equip, eff_climb):
	out = {"sid": e.designator, "rwy": ",".join(e.rwys)}
	if e.climbvia:
		out["climbvia"] = True
	diff = {eng: c for eng, c in e.climb.items() if eff_climb.get(eng) != c}
	if diff:
		if set(e.climb) == set(ENGINES) and len(set(e.climb.values())) == 1:
			out["initialClimb"] = e.climb["J"]
		else:
			out["initialClimb"] = {eng: diff[eng] for eng in ENGINES if eng in diff}
	if e.engines != set(ENGINES):
		out["engines"] = "".join(eng for eng in ENGINES if eng in e.engines)
	if e.equip != eff_equip:
		out["equip"] = e.equip
	if e.wtc:
		out["wtc"] = e.wtc
	if e.dest:
		out["dest"] = e.dest
	if e.act_arr:
		out["actArrRwy"] = e.act_arr
	if e.rules:
		out["rule"] = e.rules[0] if len(e.rules) == 1 else e.rules
	if e.areas:
		out["area"] = e.areas[0] if len(e.areas) == 1 else e.areas
	return out


def _tie_outcome(waypoint, found, entries, owner, group_of, selection):
	"""What v1 and v2 choose among tied variants without rule, all their runways active, per engine type."""
	position = {id(e): i for i, e in enumerate(entries)}
	order = selection.get(waypoint, [])

	def v2_rank(v):
		e = owner[id(v)]
		group = group_of.get(e.rwys[0])
		return (order.index(group) if group in order else len(order)), position[id(e)]

	outcomes = {}
	for eng in ENGINES:
		candidates = [v for v in found if not v.rule and eng in _engines(v)]
		if len({owner[id(v)].rwys[0] for v in candidates}) < 2:
			continue
		v1 = max(candidates, key=lambda v: v.order)
		v2 = min(candidates, key=v2_rank)
		outcomes.setdefault((v1.name, v1.rwys[0], v2.name, v2.rwys[0]), []).append(eng)
	if not outcomes:
		return "v1 takes the variant read last; check with the nav"
	if all(v1 == v2 for v1, _, v2, _ in outcomes):
		return "v1 takes the variant read last, same choice in v2 (entries ordered by prio, then last read first)"
	parts = []
	for (v1, r1, v2, r2), engines in outcomes.items():
		label = ", ".join(engines)
		if v1 == v2:
			parts.append(f"for {label} v1 and v2 take {v1} ({r1})")
		else:
			parts.append(f"for {label} v1 takes {v1} ({r1}), v2 takes {v2} ({r2})")
	return "; ".join(parts) + " (v1: variant read last, v2: runway group order of selection); to settle with the nav"


def _report_ties(by_point, group_of, selection, note):
	"""v1 equal prio between runways, and rule variants v1 and v2 do not rank alike."""
	for waypoint, entries in by_point.items():
		owner = {id(v): e for e in entries for v in e.variants}
		variants = sorted((v for e in entries for v in e.variants if not v.area), key=lambda v: v.order)
		tied = {}       # prio -> variants tied with another runway
		ranked = set()
		for i, u in enumerate(variants):
			for w in variants[i + 1:]:
				eu, ew = owner[id(u)], owner[id(w)]
				if eu is ew or not (_engines(u) & _engines(w)):
					continue
				if {"RNAV", "NON-RNAV"} <= set(u.equip) | set(w.equip) and set(u.equip) != set(w.equip):
					continue
				if u.wtc and w.wtc and not set(u.wtc) & set(w.wtc):
					continue
				if u.prio == w.prio and (not u.rule or not w.rule or u.rule == w.rule) and any(
						a != b and not _opposite(a, b) for a in u.rwys for b in w.rwys):
					tied.setdefault(u.prio, {})[u.order] = u
					tied[u.prio][w.order] = w
				rule, plain = (u, w) if u.rule and not w.rule else (w, u) if w.rule and not u.rule else (None, None)
				if rule and set(rule.rwys) & set(plain.rwys) and (id(eu), id(ew)) not in ranked and (
						rule.prio > plain.prio or (rule.prio == plain.prio and plain.order > rule.order)):
					ranked.add((id(eu), id(ew)))
					note(f"rule ranking differs: {waypoint} {rule.name} (rule {rule.rule}, prio {rule.prio}) is ranked before "
						f"{plain.name} (prio {plain.prio}) in v2, after it in v1")
		for prio, found in sorted(tied.items()):
			by_rwy = {}
			for v in sorted(found.values(), key=lambda v: v.order):
				by_rwy.setdefault(",".join(v.rwys), []).append(v.name)
			listing = ", ".join(f"{rwys} [{' '.join(names)}]" for rwys, names in by_rwy.items())
			outcome = _tie_outcome(waypoint, list(found.values()), entries, owner, group_of, selection)
			note(f"prio tie: {waypoint} at prio {prio} across runways {listing}; {outcome}")


def main(argv):
	if len(argv) != 3:
		print(__doc__.strip().splitlines()[-1], file=sys.stderr)
		return 2
	src, dst = argv[1], argv[2]
	os.makedirs(dst, exist_ok=True)
	for name in sorted(os.listdir(src)):
		if not name.endswith(".json"):
			continue
		with open(os.path.join(src, name), encoding="utf-8") as f:
			data = json.load(f)
		out = {}
		for icao, airport in data.items():
			out[icao], report = convert(load_v1(icao, airport))
			print("\n".join(report))
		with open(os.path.join(dst, name), "w", encoding="utf-8") as f:
			json.dump(out, f, indent="\t", ensure_ascii=False)
			f.write("\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
