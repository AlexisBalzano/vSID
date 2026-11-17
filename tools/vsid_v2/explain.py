"""Proves that every accepted v1/v2 difference (EQUIP aside) has a known cause.

Usage: python3 -m vsid_v2.explain <v1 dir> <cases dir> <configcases output>...

The outputs come from `CONFIGCASES_ALL=1 configcases_test ...`, which lists each accepted case as
`  ~#<index> ... expect=<v1> got=<v2> [<rule>]`. Each one is classified:
- tie: the v2 choice is one of the variants v1 can choose at the same (best) prio;
- engine-merge: engine type outside J/P/T, v1 has no SID, the v2 entry merges J and PT typed variants;
- area-engine-gap: v1 is inside an area where the v2 entry has area variants that exclude the
  engine type, while one of its plain variants accepts it (LFPG LGL A3/A4);
- no-designator: v1 chose a variant without designator, which v2 cannot hold.
Exit 1 if a case has none of these causes.
"""

import copy
import json
import os
import re
import sys

from .v1model import inside, load_v1, select_v1, split_csv

DETAIL = re.compile(r"^  ~#(\d+) .* expect=(.*) got=(.*) \[(.*)\]$")


def _eligible(ap, case):
	"""v1 variants that would be chosen alone for this case."""
	out = []
	for v in ap.variants:
		if v.waypoint != case["waypoint"]:
			continue
		one = copy.copy(ap)
		one.variants = [v]
		r = select_v1(one, case)
		if r and "sid" in r:
			out.append(v)
	return out


def classify(ap, case, got):
	"""Cause of a v1/v2 difference, None if unexplained. got = "SID RWY CLIMB", "-" or "EQUIP"."""
	expect = select_v1(ap, case)
	if got in ("-", "EQUIP"):
		return "no-designator" if expect and expect.get("sid") == "" else None
	sid, rwy = got.split(" ")[:2]
	same = [v for v in ap.variants if v.waypoint == case["waypoint"] and v.designator == sid and rwy in v.rwys]
	if expect is None:
		types = set("".join(v.engine_type for v in same))
		outside = len(case["engine"]) != 1 or case["engine"] not in "JPT"
		if outside and same and all(v.engine_type for v in same) and set("JPT") <= types:
			return "engine-merge"
		return None
	eligible = _eligible(ap, case)
	if eligible:
		best = min(v.prio for v in eligible)
		if any(v.prio == best and v.designator == sid and rwy in v.rwys for v in eligible):
			return "tie"
	position = case.get("position")
	if position and case.get("areasActive"):
		areas = {name.upper(): a for name, a in ap.areas.items()}
		here = {n for n, a in areas.items() if inside(a["points"], position["lat"], position["lon"])}
		area_variants = [v for v in same if set(split_csv(v.area)) & here]
		if area_variants and all(case["engine"] not in v.engine_type for v in area_variants if v.engine_type) \
				and any(not v.area and (not v.engine_type or case["engine"] in v.engine_type) for v in same):
			return "area-engine-gap"
	return None


def main(argv=None):
	argv = sys.argv[1:] if argv is None else argv
	if len(argv) < 3:
		print(__doc__.strip().splitlines()[2], file=sys.stderr)
		return 2
	v1_dir, cases_dir, outputs = argv[0], argv[1], argv[2:]
	counts, unexplained = {}, []
	for path in outputs:
		lines = []
		icao = None
		with open(path, encoding="utf-8") as f:
			for line in f:
				m = re.match(r"^(\w+) cases=", line)
				if m:
					icao = m.group(1)
				d = DETAIL.match(line.rstrip("\n"))
				if d and icao:
					lines.append((icao, int(d.group(1)), d.group(3), d.group(4)))
		if not lines:
			continue
		name = os.path.basename(path).split(".")[0]
		name = name[4:] if name.startswith("res_") else name
		with open(os.path.join(cases_dir, name + ".cases.json"), encoding="utf-8") as f:
			cases = json.load(f)
		with open(os.path.join(v1_dir, name + ".json"), encoding="utf-8") as f:
			v1 = json.load(f)
		airports = {}
		for icao, index, got, rule in lines:
			if icao not in airports:
				airports[icao] = load_v1(icao, v1[icao])
			case = {k: v for k, v in cases[icao][index].items() if k != "expect"}
			cause = classify(airports[icao], case, got)
			key = (icao, rule, cause or "UNEXPLAINED")
			counts[key] = counts.get(key, 0) + 1
			if cause is None:
				unexplained.append(f"{icao} #{index} [{rule}] got={got}")
	for (icao, rule, cause), n in sorted(counts.items()):
		print(f"{icao} {rule}: {n} {cause}")
	for line in unexplained[:50]:
		print("UNEXPLAINED", line)
	return 1 if unexplained else 0


if __name__ == "__main__":
	sys.exit(main())
