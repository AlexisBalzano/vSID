"""Reference model of the vSID v1 SID selection.

Mirrors ConfigParser::loadAirportConfig (configparser.cpp) for the reading of an airport and
VSIDPlugin::processSid (vSIDPlugin.cpp) for the choice, restricted to the criteria the French
configs use. configparser.h aliases json to nlohmann::ordered_json, so v1 walks every object
in file order; Python dicts keep that order too.
"""

import copy
from dataclasses import dataclass, field

CONFIG_VALUES = {"rwy", "prio", "allowDiffNumbers", "initial", "climbvia", "wpt", "trans", "pilotfiled", "wingType", "acftType",
				"dest", "route", "wtc", "engineType", "engineCount", "mtow", "customRule", "area", "equip", "lvp", "actArrRwy",
				"actDepRwy", "timeFrom", "timeTo", "sidHighlight", "clmbHighlight"}   # = configparser.h configValues

# keys each inheritance level reads (field, waypoint, designator, id); other config values are ignored there
LEVEL_KEYS = {
	1: CONFIG_VALUES - {"rwy", "prio", "trans"},
	2: CONFIG_VALUES - {"rwy", "prio"},
	3: CONFIG_VALUES - {"prio"},
	4: CONFIG_VALUES,
}

# criteria the reference model (and config v2) does not handle
UNSUPPORTED = {"allowDiffNumbers", "wpt", "trans", "pilotfiled", "wingType", "acftType", "route", "engineCount", "mtow",
			"lvp", "actDepRwy", "timeFrom", "timeTo"}


@dataclass
class Variant:
	waypoint: str
	designator: str
	vid: str
	rwys: list
	prio: int
	initial: int
	climbvia: bool
	engine_type: str
	equip: dict
	wtc: str
	dest: dict
	act_arr: dict          # {"allow": {"all", "any"}, "deny": {"all", "any"}}, parts present only if configured
	rule: str              # upper case, "" = none
	area: str              # upper case CSV, "" = none
	order: int             # traversal index inside the airport
	wing_type: str = "LSAT"

	@property
	def name(self):
		return self.designator + self.vid


@dataclass
class V1Airport:
	icao: str
	data: dict
	variants: list
	areas: dict            # name as written -> {"active", "arrAsDep", "points": [{"lat", "lon"}]}, v1 map order
	rules: dict            # customRules as written
	anomalies: list = field(default_factory=list)


def split_csv(text):
	return [p.strip() for p in str(text).split(",") if p.strip()]


def _upper_keys(d):
	return {str(k).upper(): v for k, v in d.items()}


class _Settings:
	"""tmpSidSettings: the values one level hands down to the next."""

	def __init__(self):
		self.wpt = ""
		self.desig = ""
		self.rwys = []
		self.prio = 99
		self.initial = 0
		self.via = False
		self.wing_type = "LSAT"
		self.dest = {}
		self.wtc = ""
		self.engine_type = ""
		self.custom_rule = ""
		self.area = ""
		self.equip = {"RNAV": True}
		self.act_arr = {}
		self.unsupported = set()

	def copy(self):
		return copy.deepcopy(self)

	def apply(self, key, value, path, anomalies):
		if key == "rwy":
			self.rwys = split_csv(value)
		elif key == "prio":
			self.prio = int(value)
		elif key == "initial":
			self.initial = int(value)
		elif key == "climbvia":
			self.via = bool(value)
		elif key == "wpt":
			self.wpt = value
		elif key == "wingType":
			self.wing_type = value
		elif key == "dest":
			self.dest = dict(value)
			for d in value:
				if "," in d or " " in d:
					anomalies.append(f"malformed dest: {path} '{d}' (kept as one destination, as v1 does)")
		elif key == "wtc":
			self.wtc = value
		elif key == "engineType":
			self.engine_type = value
		elif key == "customRule":
			self.custom_rule = str(value).upper()
		elif key == "area":
			self.area = str(value).upper()
		elif key == "equip":
			self.equip = _upper_keys(value)
		elif key == "actArrRwy":
			kinds = [k for k in ("allow", "deny") if k in value]
			if not kinds:
				anomalies.append(f"actArrRwy ignored by v1: {path} {value} has neither allow nor deny")
			for kind in kinds:
				self.act_arr[kind] = {"all": value[kind].get("all", ""), "any": value[kind].get("any", "")}
		if key in UNSUPPORTED:
			self.unsupported.add(key)


def _is_fixed_number(key):
	digits = sum(c.isdigit() for c in key)
	return key[-1:].isdigit() and digits > 1 and len(key) > 2


def load_v1(icao, airport):
	"""Reads one airport of a v1 file the way vSID does (four inheritance levels, file order)."""
	anomalies = []
	variants = []

	areas = {}
	for name, body in airport.get("areas", {}).items():
		points = []
		for key, coord in body.items():
			if key in ("active", "arrAsDep"):
				continue
			lat, lon = (coord.get("lat", ""), coord.get("lon", "")) if isinstance(coord, dict) else ("", "")
			if not lat or not lon:
				anomalies.append(f"area {name}: point {key} without lat/lon, v1 stops reading the area there")
				break
			points.append({"lat": lat, "lon": lon})
		if len(points) < 3:
			anomalies.append(f"area {name}: fewer than 3 points, ignored by v1")
			continue
		if body.get("arrAsDep", False):
			anomalies.append(f"area {name}: arrAsDep not modelled")
		areas[name] = {"active": bool(body.get("active", False)), "arrAsDep": bool(body.get("arrAsDep", False)), "points": points}
	areas = dict(sorted(areas.items(), key=lambda kv: kv[0].upper()))   # CustomAreaMap: case insensitive std::map

	def children(obj, level, settings, path):
		"""Applies the level's own keys in file order and yields the child keys with the settings seen so far."""
		for key, value in obj.items():
			if key in CONFIG_VALUES:
				if key in LEVEL_KEYS[level]:
					settings.apply(key, value, path, anomalies)
			elif level == 4:
				anomalies.append(f"unknown key {path}/{key} ignored")
			if level == 4:
				# id level: an empty equip falls back to RNAV after every key (configparser.cpp)
				if not settings.equip:
					settings.equip = {"RNAV": True}
				continue
			if key in CONFIG_VALUES:
				continue
			if not isinstance(value, dict):
				anomalies.append(f"non-object key {path}/{key} ignored")
				continue
			yield key, value

	field_settings = _Settings()
	for wkey, wbody in children(airport.get("sids", {}), 1, field_settings, icao):
		if _is_fixed_number(wkey):
			anomalies.append(f"fixed number waypoint key {wkey} not modelled")
		else:
			field_settings.wpt = wkey
		wpt_settings = field_settings.copy()
		for dkey, dbody in children(wbody, 2, wpt_settings, wkey):
			if not any(c.isdigit() for c in dkey) and dkey != "XXX":
				wpt_settings.desig = dkey
			des_settings = wpt_settings.copy()
			for ikey, ibody in children(dbody, 3, des_settings, f"{wkey}/{dkey}"):
				id_settings = des_settings.copy()
				path = f"{wkey}/{dkey}/{ikey}"
				for _ in children(ibody, 4, id_settings, path):
					pass
				if id_settings.unsupported:
					anomalies.append(f"unsupported criteria {sorted(id_settings.unsupported)} ignored: {path}")
				variants.append(Variant(
					waypoint=id_settings.wpt, designator=id_settings.desig, vid=ikey, rwys=list(id_settings.rwys),
					prio=id_settings.prio, initial=id_settings.initial, climbvia=id_settings.via,
					engine_type=id_settings.engine_type, equip=dict(id_settings.equip), wtc=id_settings.wtc,
					dest=dict(id_settings.dest), act_arr={k: dict(v) for k, v in id_settings.act_arr.items()},
					rule=id_settings.custom_rule, area=id_settings.area, order=len(variants),
					wing_type=id_settings.wing_type))

	return V1Airport(icao=icao, data=airport, variants=variants, areas=areas,
		rules=dict(airport.get("customRules", {})), anomalies=anomalies)


def to_degrees(coord):
	"""vsid::utils::toDeg: "N049.00.26.119" -> decimal degrees."""
	parts = [p.strip() for p in coord.split(".") if p.strip()]
	if len(parts) < 4:
		return 0.0
	sign = -1 if ("S" in parts[0] or "W" in parts[0]) else 1
	deg = float(parts[0][1:])
	minutes = float(parts[1]) / 60
	sec = (float(parts[2]) + float("0." + parts[3])) / 3600
	return (deg + minutes + sec) * sign


def inside(points, lat, lon):
	"""vsid::Area::inside (area.cpp), same edge handling."""
	poly = [(to_degrees(p["lat"]), to_degrees(p["lon"])) for p in points]
	result = False
	for i, a in enumerate(poly):
		b = poly[(i + 1) % len(poly)]
		if lat > min(a[0], b[0]) and lat <= max(a[0], b[0]) and lon <= max(a[1], b[1]):
			x_inter = (lat - a[0]) * (b[1] - a[1]) / (b[0] - a[0]) + a[1]
			if a[1] == b[1] or lon <= x_inter:
				result = not result
	return result


def _act_arr_ok(act_arr, arr_rwys):
	allow = act_arr.get("allow")
	if allow:
		if allow["all"]:
			if not all(r in arr_rwys for r in split_csv(allow["all"])):
				return False
		elif allow["any"]:
			if not any(r in arr_rwys for r in split_csv(allow["any"])):
				return False
	deny = act_arr.get("deny")
	if deny:
		if deny["all"]:
			if all(r in arr_rwys for r in split_csv(deny["all"])):
				return False
		elif deny["any"]:
			if any(r in arr_rwys for r in split_csv(deny["any"])):
				return False
	return True


def _char_in(c, text):
	"""std::string::find(char) != npos for a one character ES value."""
	return len(c) == 1 and c in text


def select_v1(ap, case):
	"""processSid for the criteria of spec §3. Returns {"sid","rwy","climb"}, {"equip": True} or None."""
	waypoint = case["waypoint"]
	dep_rwys = set(case.get("depRwys", []))
	arr_rwys = set(case.get("arrRwys", []))
	atc_rwy = case.get("atcRwy") or ""
	engine = case.get("engine", "")
	kind = case.get("aircraftKind", "?")
	equipment = case.get("equipment", "")
	pbn = case.get("pbn", "")
	wtc = case.get("wtc", "")
	dest = case.get("dest", "")
	position = case.get("position")
	areas_active = bool(case.get("areasActive", False))
	equip_check = ap.data.get("equipCheck", True)

	rules = {k.upper(): bool(v) for k, v in ap.rules.items()}
	for k, v in case.get("rules", {}).items():
		if k.upper() in rules:
			rules[k.upper()] = bool(v)
	rule_active = any(rules.values())
	areas = {name.upper(): a for name, a in ap.areas.items()}

	usable = sorted(dep_rwys | ({atc_rwy} if atc_rwy else set()))
	best = None
	best_prio = 99
	valid_equip = True

	def in_area(name):
		if not areas_active or position is None:
			return False
		return inside(areas[name]["points"], position["lat"], position["lon"])

	for v in ap.variants:
		# skip if current SID does not match found SID wpt
		if v.waypoint != waypoint and v.waypoint != "XXX":
			continue
		valid_equip = True

		# skip if SID needs active arr rwys
		if not _act_arr_ok(v.act_arr, arr_rwys):
			continue

		# skip if current SID rwys don't match dep rwys (evaluated below)
		rwy_match = any(r in v.rwys for r in usable if not atc_rwy or r == atc_rwy)

		# skip if custom rules are active but the current sid has a rule which is not active
		if rule_active and v.rule and v.rule in rules and not rules[v.rule]:
			continue

		# skip if aircraft type does not match (heli, fixed wing) - unknown aircraft is never skipped
		if kind != "?" and not _char_in(kind, v.wing_type):
			continue

		# skip if equipment does not match
		if equip_check:
			if "RNAV" in v.equip:
				if len(equipment) > 1 and not any(c in equipment for c in "ABGRI") and pbn == "":
					valid_equip = False
					continue
			elif "NON-RNAV" in v.equip:
				if len(equipment) < 2 and pbn == "":
					valid_equip = False
					continue
				if any(c in equipment for c in "GRI") or pbn != "":
					valid_equip = False
					continue
			else:
				# the v1 inner loop "continue" only flags the variant, it never skips it
				for code, required in v.equip.items():
					if (required and code not in equipment) or (not required and code in equipment):
						valid_equip = False

		# skip if custom rules are inactive but a rule exists in sid
		if not rule_active and v.rule:
			continue

		# area checks
		if v.area:
			names = split_csv(v.area)
			if all(n in areas and not areas_active for n in names):
				continue
			if not any(n in areas and in_area(n) for n in names):
				continue

		# skip if no matching rwy was found in SID
		if not rwy_match:
			continue

		# skip if engine type doesn't match
		if v.engine_type and not _char_in(engine, v.engine_type):
			continue

		# skip if SID has WTC requirement and acft doesn't match
		if v.wtc and not _char_in(wtc, v.wtc):
			continue

		# skip if destination restrictions present and flight plan doesn't match
		if v.dest:
			if dest in v.dest and not v.dest[dest]:
				continue
			if dest not in v.dest and any(v.dest.values()):
				continue

		if v.prio == 0:
			return None   # forced manual selection
		if v.prio == 99:
			continue
		if v.prio > best_prio:
			continue
		best = v
		best_prio = v.prio

	if best is None:
		return {"equip": True} if not valid_equip else None

	# runway: the ATC runway if the SID has it, else the first SID runway that is an active departure runway
	if atc_rwy and atc_rwy in best.rwys:
		rwy = atc_rwy
	else:
		rwy = next((r for r in best.rwys if r in dep_rwys), "")
	return {"sid": best.designator, "rwy": rwy, "climb": best.initial}
