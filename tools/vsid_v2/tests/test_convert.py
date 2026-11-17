import json
import os
import unittest

from vsid_v2.v1model import load_v1
from vsid_v2.convert import convert

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def converted(name):
	with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
		data = json.load(f)
	(icao, airport), = data.items()
	return convert(load_v1(icao, airport))


def entries_of(airport, waypoint):
	body = airport["procedures"][waypoint]
	return body["entries"] if isinstance(body, dict) else body


def check_v2_shape(test, ap):
	"""The refusals of parseAirport (sidconfigv2.cpp) that the converter can cause."""
	test.assertEqual(ap["version"], 2)
	runways = set(ap.get("runways", "").split(","))
	rules = set(ap.get("customRules", {}))
	groups = set(ap.get("runwayGroups", {}))
	for waypoint, body in ap["procedures"].items():
		if isinstance(body, dict):
			test.assertEqual(set(body) - {"defaults", "entries"}, set())
		for e in entries_of(ap, waypoint):
			test.assertTrue(e["sid"])
			for rwy in e["rwy"].split(","):
				test.assertIn(rwy, runways)
			rule = e.get("rule", [])
			for r in [rule] if isinstance(rule, str) else rule:
				test.assertIn(r, rules)
			test.assertTrue(set(e.get("engines", "")) <= set("JPT"))
			area = e.get("area", [])
			for a in [area] if isinstance(area, str) else area:
				test.assertIn(a, ap.get("areas", {}))
	for waypoint, order in ap.get("selection", {}).items():
		test.assertIn(waypoint, ap["procedures"])
		test.assertTrue(set(order) <= groups)
	for area in ap.get("areas", {}).values():
		test.assertTrue(area["group"] == "default" or area["group"] in groups)
		test.assertGreaterEqual(len(area["points"]), 3)
	for sid, strategy in ap.get("strategies", {}).items():
		test.assertNotIn(" ", sid)
		test.assertNotEqual(sid.lower(), "nominal")
		for key, mode in strategy["assign"].items():
			test.assertTrue(key == "*" or key in ap["procedures"])
			test.assertTrue(mode in ("nearest", "default") or mode in groups)
	if "defaultStrategy" in ap:
		test.assertIn(ap["defaultStrategy"], ap["strategies"])


class ConvertTest(unittest.TestCase):
	def test_dikol_entries(self):
		ap, _ = converted("lfpg_dikol_lgl.json")
		dikol = entries_of(ap, "DIKOL")
		# v1 choice order: prio ascending, then the variant v1 reads last first (it wins a tie)
		self.assertEqual([e["sid"] for e in dikol], ["K", "D", "G", "A", "L", "E", "H", "B"])
		self.assertEqual(dikol[0], {"sid": "K", "rwy": "09L,09R", "rule": "opposing"})
		self.assertEqual(dikol[-1], {"sid": "B", "rwy": "26R,26L", "climbvia": True})
		self.assertEqual({k: set(v) for k, v in ap["runwayGroups"].items()},
			{"north": {"27L", "27R", "09L", "09R"}, "south": {"26R", "26L", "08R", "08L"}})
		self.assertEqual(ap["selection"]["DIKOL"], ["north", "south"])
		self.assertEqual(ap["defaults"], {"equip": {"RNAV": True}, "initialClimb": {"J": 10000, "P": 5000, "T": 5000}})
		self.assertEqual(ap["areas"]["south"]["group"], "south")
		self.assertEqual(ap["areas"]["south"]["points"][0], {"lat": "N049.00.26.119", "lon": "E002.31.27.453"})

	def test_lfpg_strategies(self):
		ap, report = converted("lfpg_dikol_lgl.json")
		self.assertEqual(ap["defaultStrategy"], "roulage-mini")
		self.assertEqual(list(ap["strategies"]), ["roulage-mini", "ranux-dikol-sud", "lanvi-bubli-baxir-nord"])
		self.assertEqual(ap["strategies"]["roulage-mini"]["assign"], {"*": "nearest"})
		# the reduced fixture has neither RANUX nor LANVI/BUBLI/BAXIR
		self.assertEqual(ap["strategies"]["ranux-dikol-sud"]["assign"], {"DIKOL": "south"})
		self.assertTrue(any("RANUX" in line and "strategy" in line for line in report))

	def test_lfbo_groups(self):
		ap, report = converted("lfbo_amolo.json")
		self.assertEqual({k: set(v) for k, v in ap["runwayGroups"].items()},
			{"airbus": {"14R", "32L"}, "other": {"14L", "32R"}})
		self.assertEqual(ap["defaultStrategy"], "nearest")
		self.assertEqual(ap["strategies"], {"nearest": {"label": "Nearest", "assign": {"*": "nearest"}}})
		self.assertEqual(ap["selection"]["AMOLO"], ["other", "airbus"])
		self.assertEqual([(e["sid"], e["rwy"]) for e in entries_of(ap, "AMOLO")],
			[("B", "32R"), ("B", "32L"), ("H", "14R"), ("A", "14R"), ("H", "14L"), ("A", "14L")])
		self.assertTrue(any("prio tie" in line and "H2" in line and "H3" in line for line in report))
		# measured on the replay: with 14L and 14R active v1 gives 14R (H3, read last), v2 14L (group other first)
		tie = next(line for line in report if line.startswith("LFBO: prio tie: AMOLO"))
		self.assertIn("for P, T v1 takes H3 (14R), v2 takes H2 (14L)", tie)
		self.assertIn("to settle with the nav", tie)
		self.assertNotIn("keeps the v1 reading order", tie)
		self.assertTrue(any("area-only entry" in line and "A1" in line for line in report))

	def test_lfbo_area_only_entry(self):
		ap, _ = converted("lfbo_amolo.json")
		amolo = entries_of(ap, "AMOLO")
		# A1 (14R) exists in v1 only inside the airbus area; B1 and H1 have a plain twin (B3, H3).
		# Inside the area v1 takes H1 over A1 (same prio, read last): the area entry goes after H 14R.
		self.assertEqual([(e["sid"], e["rwy"], e.get("area")) for e in amolo],
			[("B", "32R", None), ("B", "32L", None), ("H", "14R", None), ("A", "14R", "airbus"), ("H", "14L", None),
				("A", "14L", None)])
		check_v2_shape(self, ap)

	def test_report_engine_split_anomalies(self):
		lgl = {"initial": 5000, "A": {"rwy": "27L",
			"1": {"prio": 6, "engineType": "J"}, "2": {"prio": 6, "engineType": "PT"},
			"3": {"prio": 2, "engineType": "PT", "area": "north"}}}
		nurmo = {"initial": 5000, "G": {"rwy": "09L",
			"1": {"prio": 4, "engineType": "J"}, "2": {"prio": 4, "engineType": "PT", "climbvia": True}}}
		north = {"active": True, "p1": {"lat": "N049.00.00.000", "lon": "E002.30.00.000"},
			"p2": {"lat": "N049.01.00.000", "lon": "E002.30.00.000"}, "p3": {"lat": "N049.01.00.000", "lon": "E002.31.00.000"}}
		ap = load_v1("LFPG", {"runways": "27L,09L", "sids": {"LGL": lgl, "NURMO": nurmo}, "areas": {"north": north}})
		_, report = convert(ap)
		text = "\n".join(report)
		self.assertIn("area variants only for engines PT: LGL A (27L)", text)
		self.assertIn("climbvia differs by engine type: NURMO G (09L)", text)

	def test_lfpn_rule_list(self):
		ap, _ = converted("lfpn_rules.json")
		atrex = entries_of(ap, "ATREX")
		# each LFPN designator carries two of the four rules (no designator has all four)
		self.assertEqual([(e["sid"], e["rule"]) for e in atrex],
			[("V", ["eipg", "elpg"]), ("P", ["wipg", "wlpg"]), ("B", ["wipg", "wlpg"]), ("A", ["eipg", "elpg"])])

	def test_report_anomalies(self):
		ap, report = converted("anomalies.json")
		text = "\n".join(report)
		for needle in ("actArrRwy ignored by v1", "malformed dest", "dropped key", "prio tie"):
			self.assertIn(needle, text)
		self.assertNotIn("CMB", ap)
		tinot = {e["sid"]: e for e in entries_of(ap, "TINOT")}
		self.assertNotIn("actArrRwy", tinot["R"])
		self.assertEqual(tinot["K"]["actArrRwy"], {"allow": {"all": "20"}})
		kerit = {e["sid"]: e for e in entries_of(ap, "KERIT")}
		self.assertEqual(kerit["Q"]["dest"], {"LFMN,LFMD LFTZ": True})

	def test_lfpo_prio_tie(self):
		_, report = converted("lfpo_tie.json")
		self.assertTrue(any("prio tie" in line and "ATREX" in line for line in report))
		# without runway groups v2 lists the variant v1 reads last first: same choice
		tie = next(line for line in report if "prio tie: ATREX" in line)
		self.assertIn("same choice in v2", tie)

	def test_output_parses_in_cpp_shape(self):
		for name in ("lfpg_dikol_lgl.json", "lfbo_amolo.json", "lfpo_tie.json", "lfpn_rules.json", "anomalies.json"):
			with self.subTest(name=name):
				ap, report = converted(name)
				self.assertEqual(list(ap)[0], "version")
				for key in ("version", "runways", "defaults", "procedures"):
					self.assertIn(key, ap)
				self.assertNotIn("sids", ap)
				check_v2_shape(self, ap)
				self.assertTrue(report[0].endswith("entries"))
		lfpg, _ = converted("lfpg_dikol_lgl.json")
		for key in ("elevation", "transAlt", "maxInitialClimb", "appSI", "customRules", "intersections",
				"runwayGroups", "areas", "selection", "strategies", "defaultStrategy"):
			self.assertIn(key, lfpg)


if __name__ == "__main__":
	unittest.main()
