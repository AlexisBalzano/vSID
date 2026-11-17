import json
import os
import unittest

from vsid_v2.explain import classify
from vsid_v2.v1model import load_v1

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")
LFPG_NORTH = {"lat": 49.016, "lon": 2.54}


def fixture(name):
	with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
		data = json.load(f)
	(icao, airport), = data.items()
	return load_v1(icao, airport)


def case(**overrides):
	c = {"waypoint": "AMOLO", "depRwys": ["14R", "14L"], "arrRwys": ["14R", "14L"], "atcRwy": "", "engine": "P",
		"aircraftKind": "L", "equipment": "SDFGRY", "pbn": "A1B1C1D1", "wtc": "M", "dest": "ZZZZ", "position": None,
		"rules": {}, "areasActive": False, "mode": "default"}
	c.update(overrides)
	return c


class ClassifyTest(unittest.TestCase):
	def test_tie(self):
		# H2 (14L) and H3 (14R) at prio 2: v1 takes H3, a v2 H 14L is the other half of the tie
		lfbo = fixture("lfbo_amolo.json")
		self.assertEqual(classify(lfbo, case(), "H 14L 7000"), "tie")

	def test_unexplained(self):
		lfbo = fixture("lfbo_amolo.json")
		self.assertIsNone(classify(lfbo, case(), "A 14L 7000"))   # A is JT only: not a P choice
		self.assertIsNone(classify(lfbo, case(), "-"))

	def test_area_engine_gap(self):
		lfpg = fixture("lfpg_dikol_lgl.json")
		c = case(waypoint="LGL", depRwys=["26R", "27L"], arrRwys=["26L", "27R"], engine="J", position=LFPG_NORTH,
			areasActive=True, mode="nearest")
		self.assertEqual(classify(lfpg, c, "A 27L 10000"), "area-engine-gap")
		# a turboprop gets A3/A4 inside the area in v1: B 26R would be a real difference
		self.assertIsNone(classify(lfpg, dict(c, engine="T"), "B 26R 5000"))


if __name__ == "__main__":
	unittest.main()
