"""Generated test cases of one airport (spec §6 step 4).

Each case holds the inputs of a SID request (v1 model and v2 engine alike) and "expect", the
choice of the v1 reference model. tests/configcases_test.cpp replays them on the v2 config.
"""

import random

from .v1model import inside, select_v1, split_csv, to_degrees

MAX_CASES = 20000
SEED = 20261117

# ES engine types: J, P, T, plus E (electric) for the engine types v2 lets through where v1 had none
ENGINES = ["J", "P", "T", "E"]
EQUIPMENT = [("SDFGRY", "A1B1C1D1"), ("SDFY", "")]   # RNAV, then not RNAV
WTCS = ["M", "L"]
OTHER_DEST = "ZZZZ"


def _sense(rwy):
	"""0 for a runway heading 010-180, 1 for 190-360 (parallel runways share a sense)."""
	digits = rwy.rstrip("LCR")
	return 0 if digits.isdigit() and 1 <= int(digits) <= 18 else 1


def runway_configurations(ap, v2):
	"""Active departure runways: one configuration per direction from the runway groups;
	without groups each runway alone, each direction with parallel runways, then all."""
	runways = split_csv(ap.data.get("runways", ""))
	groups = v2.get("runwayGroups", {})
	configs = []

	def add(rwys):
		if rwys and rwys not in configs:
			configs.append(rwys)

	if groups:
		for sense in (0, 1):
			add([r for g in groups.values() for r in g if _sense(r) == sense])
	else:
		for r in runways:
			add([r])
		for sense in (0, 1):
			same = [r for r in runways if _sense(r) == sense]
			if len(same) > 1:
				add(same)
		add(runways)
	return configs


def _centroid(points):
	"""A point inside the polygon: the mean of its vertices, else the area centroid, else a grid point."""
	poly = [(to_degrees(p["lat"]), to_degrees(p["lon"])) for p in points]
	candidates = [(sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))]
	area = cx = cy = 0.0
	for i, a in enumerate(poly):
		b = poly[(i + 1) % len(poly)]
		cross = a[0] * b[1] - b[0] * a[1]
		area += cross
		cx += (a[0] + b[0]) * cross
		cy += (a[1] + b[1]) * cross
	if area:
		candidates.append((cx / (3 * area), cy / (3 * area)))
	lats = [p[0] for p in poly]
	lons = [p[1] for p in poly]
	for i in range(1, 20):
		for j in range(1, 20):
			candidates.append((min(lats) + (max(lats) - min(lats)) * i / 20, min(lons) + (max(lons) - min(lons)) * j / 20))
	for lat, lon in candidates:
		if inside(points, lat, lon):
			return {"lat": round(lat, 7), "lon": round(lon, 7)}
	return None


def positions(ap):
	"""The centroid of each v1 area, then no position."""
	found = []
	for body in ap.areas.values():
		p = _centroid(body["points"])
		if p and p not in found:
			found.append(p)
	return found + [None]


def rule_states(ap):
	"""Every rule off, then each rule on alone."""
	names = list(ap.rules)
	states = [{n: False for n in names}]
	for name in names:
		states.append({n: n == name for n in names})
	return states


def destinations(ap):
	cited = []
	for v in ap.variants:
		for d in v.dest:
			if d not in cited:
				cited.append(d)
	return cited + [OTHER_DEST]


def dimensions(ap, v2):
	waypoints = []
	for v in ap.variants:
		if v.waypoint and v.waypoint not in waypoints:
			waypoints.append(v.waypoint)
	area_states = [False, True] if ap.areas else [False]
	return [waypoints, runway_configurations(ap, v2), rule_states(ap), ENGINES, EQUIPMENT, WTCS, destinations(ap),
		positions(ap), area_states]


def _case(values):
	waypoint, rwys, rules, engine, (equipment, pbn), wtc, dest, position, areas_active = values
	return {
		"waypoint": waypoint,
		"depRwys": list(rwys),
		"arrRwys": list(rwys),
		"atcRwy": "",
		"engine": engine,
		"aircraftKind": "L",
		"equipment": equipment,
		"pbn": pbn,
		"wtc": wtc,
		"dest": dest,
		"position": position,
		"rules": dict(rules),
		"areasActive": areas_active,
		"mode": "nearest" if areas_active else "default",
	}


def generate(ap, v2):
	"""Cases of one airport with "expect" = select_v1, at most MAX_CASES (fixed seed sampling)."""
	dims = dimensions(ap, v2)
	total = 1
	for d in dims:
		total *= len(d)
	if total > MAX_CASES:
		indices = sorted(random.Random(SEED).sample(range(total), MAX_CASES))
	else:
		indices = range(total)

	cases = []
	for index in indices:
		values = []
		for d in reversed(dims):
			index, i = divmod(index, len(d))
			values.append(d[i])
		c = _case(list(reversed(values)))
		c["expect"] = select_v1(ap, c)
		cases.append(c)
	return cases
