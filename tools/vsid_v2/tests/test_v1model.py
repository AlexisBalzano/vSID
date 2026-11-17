import json
import os
import unittest

from vsid_v2.v1model import load_v1, select_v1

FIXTURES = os.path.join(os.path.dirname(__file__), "fixtures")

# points well inside the real v1 polygons
LFPG_SOUTH = {"lat": 48.998, "lon": 2.57}
LFPG_NORTH = {"lat": 49.016, "lon": 2.54}
OUTSIDE = {"lat": 48.0, "lon": 2.0}


def fixture(name):
	with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
		data = json.load(f)
	(icao, airport), = data.items()
	return load_v1(icao, airport)


def case(**overrides):
	c = {
		"waypoint": "DIKOL",
		"depRwys": ["26R", "27L"],
		"arrRwys": ["26L", "27R"],
		"atcRwy": "",
		"engine": "J",
		"aircraftKind": "L",
		"equipment": "SDFGIRWY",
		"pbn": "A1B1C1D1",
		"wtc": "M",
		"dest": "EGLL",
		"position": None,
		"rules": {},
		"areasActive": True,
		"mode": "default",
	}
	c.update(overrides)
	return c


class OrderTest(unittest.TestCase):
	def test_order_is_file_order(self):
		# configparser.h aliases json to nlohmann::ordered_json: v1 walks keys in file order
		lfpg = fixture("lfpg_dikol_lgl.json")
		dikol = [v.designator + v.vid for v in lfpg.variants if v.waypoint == "DIKOL"]
		self.assertEqual(dikol[:5], ["A1", "A2", "B1", "B2", "B3"])
		self.assertEqual(len(dikol), 24)
		self.assertEqual(sorted({v.waypoint for v in lfpg.variants}), ["DIKOL", "LGL"])
		self.assertEqual([v.order for v in lfpg.variants], list(range(len(lfpg.variants))))
		# LFBO AMOLO declares A, H, B: kept as declared, not sorted
		lfbo = fixture("lfbo_amolo.json")
		self.assertEqual([v.designator + v.vid for v in lfbo.variants],
			["A1", "A2", "H1", "H2", "H3", "B1", "B2", "B3"])

	def test_inheritance_four_levels(self):
		lfbo = fixture("lfbo_amolo.json")
		a1 = lfbo.variants[0]
		self.assertEqual(a1.engine_type, "JT")        # designator level
		self.assertEqual(a1.initial, 7000)            # waypoint level
		self.assertEqual(a1.equip, {"RNAV": True})    # waypoint level
		self.assertEqual(a1.area, "AIRBUS")           # id level, upper case as v1
		self.assertEqual(a1.rwys, ["14R"])
		self.assertEqual(a1.prio, 1)
		self.assertEqual(lfbo.areas["airbus"]["active"], True)
		self.assertEqual(len(lfbo.areas["airbus"]["points"]), 4)


class SelectTest(unittest.TestCase):
	def setUp(self):
		self.lfpg = fixture("lfpg_dikol_lgl.json")

	def test_tie_last_wins(self):
		lfbo = fixture("lfbo_amolo.json")
		for engine in ("P", "T"):
			got = select_v1(lfbo, case(waypoint="AMOLO", depRwys=["14L", "14R"], arrRwys=[], engine=engine,
				position=OUTSIDE))
			self.assertEqual(got, {"sid": "H", "rwy": "14R", "climb": 7000}, engine)

	def test_area_variant_when_active(self):
		got = select_v1(self.lfpg, case(position=LFPG_SOUTH, areasActive=True))
		self.assertEqual(got, {"sid": "B", "rwy": "26R", "climb": 10000})

	def test_area_ignored_when_inactive(self):
		got = select_v1(self.lfpg, case(position=LFPG_SOUTH, areasActive=False))
		self.assertEqual(got, {"sid": "A", "rwy": "27L", "climb": 10000})

	def test_outside_any_area(self):
		got = select_v1(self.lfpg, case(position=OUTSIDE, areasActive=True))
		self.assertEqual(got, {"sid": "A", "rwy": "27L", "climb": 10000})

	def test_lgl_jet_north_goes_south(self):
		# LGL A3/A4 are typed PT only: a jet parked north gets no north variant and departs 26
		got = select_v1(self.lfpg, case(waypoint="LGL", depRwys=["26R", "27L"], position=LFPG_NORTH))
		self.assertEqual(got, {"sid": "B", "rwy": "26R", "climb": 10000})
		prop = select_v1(self.lfpg, case(waypoint="LGL", depRwys=["26R", "27L"], position=LFPG_NORTH, engine="T"))
		self.assertEqual(prop, {"sid": "A", "rwy": "27L", "climb": 5000})

	def test_equip_flag(self):
		got = select_v1(self.lfpg, case(equipment="SDFLOY", pbn=""))
		self.assertEqual(got, {"equip": True})

	def test_atc_runway(self):
		got = select_v1(self.lfpg, case(atcRwy="26L"))
		self.assertEqual(got, {"sid": "B", "rwy": "26L", "climb": 10000})

	def test_rule_active(self):
		got = select_v1(self.lfpg, case(rules={"opposing": True}))
		self.assertEqual(got, {"sid": "D", "rwy": "27L", "climb": 10000})

	def test_no_runway_no_sid(self):
		self.assertIsNone(select_v1(self.lfpg, case(depRwys=[], arrRwys=[])))

	def test_opposing_tie_last_wins(self):
		lfpo = fixture("lfpo_tie.json")
		base = dict(waypoint="ATREX", depRwys=["20"], arrRwys=["24"], wtc="M")
		self.assertEqual(select_v1(lfpo, case(**base)), {"sid": "A", "rwy": "20", "climb": 8000})
		self.assertEqual(select_v1(lfpo, case(rules={"opposing": True}, **base)), {"sid": "B", "rwy": "20", "climb": 8000})

	def test_wtc(self):
		lfpo = fixture("lfpo_tie.json")
		got = select_v1(lfpo, case(waypoint="ATREX", depRwys=["20"], arrRwys=[], wtc="H"))
		self.assertIsNone(got)


if __name__ == "__main__":
	unittest.main()
