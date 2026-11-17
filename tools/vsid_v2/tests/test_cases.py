import contextlib
import io
import json
import os
import shutil
import tempfile
import unittest

from vsid_v2.v1model import load_v1, select_v1
from vsid_v2.convert import convert
from vsid_v2.cases import MAX_CASES, generate
from vsid_v2.__main__ import main

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")


def fixture(name):
	with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
		data = json.load(f)
	(icao, airport), = data.items()
	ap = load_v1(icao, airport)
	return ap, convert(ap)[0]


def big_airport():
	"""30 waypoints and 30 destinations: far more combinations than the cap."""
	sids = {}
	for i in range(30):
		dests = {f"LF{chr(65 + i)}{chr(65 + j)}": True for j in range(30)}
		sids[f"WPT{chr(65 + i)}"] = {"A": {"1": {"rwy": "08", "prio": 1, "dest": dests}},
			"B": {"1": {"rwy": "26", "prio": 1}}}
	ap = load_v1("LFXX", {"runways": "08,26", "sids": sids})
	return ap, convert(ap)[0]


class GenerateTest(unittest.TestCase):
	def test_generate_deterministic(self):
		for ap, v2 in (fixture("lfpg_dikol_lgl.json"), big_airport()):
			self.assertEqual(generate(ap, v2), generate(ap, v2))

	def test_case_has_expect(self):
		ap, v2 = fixture("lfpg_dikol_lgl.json")
		cases = generate(ap, v2)
		self.assertTrue(cases)
		for c in cases:
			self.assertIn("expect", c)
			inputs = {k: v for k, v in c.items() if k != "expect"}
			self.assertEqual(c["expect"], select_v1(ap, inputs))
		# the v2 mode follows the v1 area state
		self.assertEqual({(c["areasActive"], c["mode"]) for c in cases}, {(False, "default"), (True, "nearest")})
		# one runway configuration per direction, from the runway groups
		self.assertEqual({frozenset(c["depRwys"]) for c in cases},
			{frozenset({"09L", "09R", "08R", "08L"}), frozenset({"27L", "27R", "26R", "26L"})})
		self.assertEqual({c["engine"] for c in cases}, {"J", "P", "T", "E"})
		self.assertEqual({c["wtc"] for c in cases}, {"M", "L"})
		# the centroid of each area, then no position
		positions = {json.dumps(c["position"]) for c in cases}
		self.assertEqual(len(positions), 3)
		self.assertIn("null", positions)
		self.assertTrue(any(c["expect"] and c["expect"].get("sid") for c in cases))

	def test_runway_configurations_without_groups(self):
		ap, v2 = fixture("lfpo_tie.json")
		configs = {tuple(c["depRwys"]) for c in generate(ap, v2)}
		runways = ap.data["runways"].split(",")
		for r in runways:
			self.assertIn((r,), configs)
		self.assertIn(tuple(runways), configs)

	def test_rules_off_then_each_alone(self):
		ap, v2 = fixture("lfpn_rules.json")
		states = {json.dumps(c["rules"], sort_keys=True) for c in generate(ap, v2)}
		self.assertEqual(len(states), 5)
		self.assertIn(json.dumps({r: False for r in ap.rules}, sort_keys=True), states)

	def test_cap_20000(self):
		ap, v2 = big_airport()
		cases = generate(ap, v2)
		self.assertEqual(len(cases), MAX_CASES)
		self.assertEqual(MAX_CASES, 20000)
		self.assertEqual(len({json.dumps(c, sort_keys=True) for c in cases}), MAX_CASES)
		self.assertEqual(len({c["waypoint"] for c in cases}), 30)


class CliTest(unittest.TestCase):
	def test_writes_config_cases_and_report(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(tmp, "v1")
			os.makedirs(src)
			shutil.copy(os.path.join(FIXTURES, "lfbo_amolo.json"), os.path.join(src, "lfbo.json"))
			report = os.path.join(tmp, "report.md")
			with contextlib.redirect_stdout(io.StringIO()):
				self.assertEqual(main([src, os.path.join(tmp, "v2"), "--cases", os.path.join(tmp, "cases"), "--report", report]), 0)
			with open(os.path.join(tmp, "v2", "lfbo.json"), encoding="utf-8") as f:
				self.assertEqual(json.load(f)["LFBO"]["version"], 2)
			with open(os.path.join(tmp, "cases", "lfbo.cases.json"), encoding="utf-8") as f:
				cases = json.load(f)["LFBO"]
			ap, v2 = fixture("lfbo_amolo.json")
			self.assertEqual(cases, generate(ap, v2))
			with open(report, encoding="utf-8") as f:
				text = f.read()
			self.assertIn("| LFBO | 8 |", text)
			self.assertIn("Differences to settle with the nav", text)


if __name__ == "__main__":
	unittest.main()
