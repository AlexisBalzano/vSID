## vSID

vSID is a powerful plugin for Euroscope that assists controllers with SID selection, clearances and some additional features.

The plugin is used within several vACCs by default.

## Feature List
* Configuration files for great variation and different conditions. Airport files use the format v2 (see [SID configuration v2](#sid-configuration-v2)); the Github wiki describes the other settings.
* SID strategies per airport (runway group per exit point, nearest runway) switched with a command or by another plugin through the bridge.
* SID/RWY suggestion based on the config file and the filed flight plan.
* Initial climb suggestion based on the SID.
* Different colors for SID, climb, runway to indicate any deviations from default assigment.
* Optimized menus für SIDs, runways, climbs, etc.
* Request menu for different requests as clearance, startup, pushback, taxi, vfr.
* Startup counter per runway (all "active" flights with startup approval).
* Intersection menu to set pre-defined or custom runway intersections as "cleared" or "able for" (also available as text entry in the scratch pad - see the wiki for details)
* Optional pushback, request and intersection indicator next to the radar target.

## Default colors
* SID
  * suggested - white
  * custom suggestion - yellow
  * set - green
  * custom set - orange
  * error - red
* Initial climb
  * suggested - white
  * set climb - cyan
  * set climb via - green
  * set custom - orange
* Runway
  * suggested - white
  * set - green
  * set, no dep rwy - yellow

**Coloring examples**

![image](https://github.com/user-attachments/assets/0fc232ac-dd57-4e1f-ae6b-d4c778abf954)

**Menu example**

![image](https://github.com/user-attachments/assets/1c8e2d02-44e2-4676-aeda-410ffcddcb92)

## SID configuration v2

Airport files must use the format v2: each airport object carries `"version": 2`.
A file without it (the former v1 `sids` format) is rejected with a message in the
vSID chat and the airport is not loaded. The airport settings that do not concern
the SID choice stay as before (`elevation`, `runways`, `transAlt`, `maxInitialClimb`,
`appSI`, `intersections`, `customRules`, ...). The SID part is made of `procedures`,
`selection`, `runwayGroups`, `defaults`, `areas` and `strategies`.

```json
{
  "LFPG": {
    "version": 2,
    "runways": "08R,08L,09R,09L,26L,26R,27L,27R",
    "customRules": { "opposing": false },
    "runwayGroups": {
      "north": ["09L", "09R", "27L", "27R"],
      "south": ["08L", "08R", "26L", "26R"]
    },
    "defaults": {
      "equip": { "RNAV": true },
      "initialClimb": { "J": 10000, "P": 5000, "T": 5000 }
    },
    "areas": {
      "north": { "group": "north", "points": [
        { "lat": "N049.00.43.530", "lon": "E002.35.01.485" }, { "lat": "...", "lon": "..." }, { "lat": "...", "lon": "..." } ] }
    },
    "procedures": {
      "DIKOL": [
        { "sid": "D", "rwy": "27L,27R", "rule": "opposing" },
        { "sid": "A", "rwy": "27L,27R" },
        { "sid": "B", "rwy": "26R,26L", "climbvia": true }
      ],
      "RANUX": [
        { "sid": "A", "rwy": "27L,27R" },
        { "sid": "B", "rwy": "26R,26L", "climbvia": true }
      ],
      "OKASI": {
        "defaults": { "initialClimb": { "J": 12000 } },
        "entries": [ { "sid": "B", "rwy": "26R,26L", "engines": "J" } ]
      }
    },
    "selection": { "DIKOL": ["north", "south"], "RANUX": ["north", "south"] },
    "defaultStrategy": "roulage-mini",
    "strategies": {
      "roulage-mini": { "label": "Roulage mini", "assign": { "*": "nearest" } },
      "ranux-dikol-sud": { "label": "RANUX DIKOL -> Sud", "assign": { "RANUX": "south", "DIKOL": "south" } }
    }
  }
}
```

**Procedures.** For each exit point, an ordered list of entries. The full SID name
(point, number, designator) comes from the sector file, as before. Entry fields:

| Field | Meaning |
| --- | --- |
| `sid` | Designator, required. |
| `rwy` | Departure runways (comma separated), required. |
| `climbvia` | Climb via SID (true/false). |
| `initialClimb` | Number (all engine types) or `{ "J": ..., "P": ..., "T": ... }`. Absent: `defaults.initialClimb`. |
| `engines` | Engine types allowed, any of `J`, `P`, `T` (e.g. `"PT"`). Absent: all. |
| `equip` | `RNAV`, `NON-RNAV` or equipment codes to true/false. Absent: `defaults.equip`; `{}` means no requirement. |
| `wtc` | Wake categories allowed (e.g. `"HJ"`). |
| `dest` | Destination ICAO to true/false. |
| `actArrRwy` | `allow`/`deny` x `all`/`any` active arrival runways, as in v1. |
| `rule` | Rule name or list: the entry is eligible only if at least one of them is active. |
| `area` | Area name or list: the entry is eligible only when the point's mode is `nearest` and the aircraft is inside one of them. |

An exit point may also be written `{ "defaults": {...}, "entries": [...] }` to
override the airport `defaults` for its entries.

**Choice.** vSID keeps the entries whose runway is usable (the active departure
runways, or only the runway set by ATC) and that pass every filter. It then takes the
groups in the order given by the point's mode, and in the first group that has an
eligible entry it picks the first entry with an active `rule`, else the first entry
in file order. Without `runwayGroups` all entries form one group. If nothing is left
and an entry was only dropped because of the equipment, the SID shows `EQUIP`.

**Areas.** `areas` maps a name to `{ "group": ..., "points": [...] }`: a polygon of
at least 3 points (`lat`/`lon` as in the sector file) and a group of `runwayGroups`
or `default`. An area has no on/off state; it only matters in mode `nearest`.

**Modes.** The mode of an exit point decides the group order:
* `default` - the order of `selection[point]` (absent: one group with all entries);
* a group name, e.g. `south` - that group first, then the rest of `selection[point]`;
* `nearest` - the group of the area the aircraft is in (an area of group `default`
  means `default`; if the aircraft is in several areas, a `default` area wins, then
  the first declared); outside any area, `default`.

A runway set by ATC wins over the mode: only its group can give a SID.

**Strategies.** `strategies` maps an id (no space, case-insensitive; `nominal` and
`resume` are reserved) to a `label` and an `assign` object from exit point (or `*`
for every other point) to mode. The nominal strategy puts every point in `default`.
`defaultStrategy` is the strategy active when the airport is first activated in the
session; absent, the airport starts in `nominal`. The mode of a point
is its override (`.vsid assign`), else its entry in the active strategy, else the
strategy's `*`, else `default`. The state lasts for the session and survives airport
deactivation, reactivation and `.vsid reload`.

```text
.vsid strategy LFPG                      shows the active strategy, overrides and source
.vsid strategy LFPG ranux-dikol-sud      activates a strategy and clears the overrides
.vsid strategy LFPG nominal              back to nominal, clears the overrides
.vsid assign LFPG DIKOL=south LANVI=nearest RANUX=default
.vsid assign LFPG clear                  clears the overrides
.vsid strategy LFPG resume               hands the airport back to the external source
```

`.vsid assign` is validated as a whole: an unknown point or mode changes nothing. A
change works like a rule change: uncleared flights of airports in SID-assignment
Auto are processed again, the others get a fresh suggestion. A SID already set in
the route (ATC block `SID/RWY`) does not change.

**External source.** With `"strategySource": "<provider>/<field>"` in
`vSidConfig.json` (optional), vSID subscribes to that bridge field and applies it
on change. Expected value, per airport: `{"LFPG":{"strategy":"ranux-dikol-sud","assign":{"DIKOL":"south"}}}`
(`strategy` may be `nominal`; `strategy` alone clears the overrides, `assign` alone
replaces them). Airports missing from the value are left alone; an invalid value is
ignored with a warning. A manual `.vsid strategy` or `.vsid assign` makes the airport
local: the source is ignored for it until `.vsid strategy ICAO resume`.

The `.vsid area`, `.vsid lvp` and `.vsid night` / `.vsid time` commands are gone, with
the v1 SID settings `lvp`, `timeFrom`/`timeTo`, `pilotfiled`, `trans`, `route`,
`mtow`, `acftType`, `engineCount`, `wingType`, `actDepRwy`, `allowDiffNumbers`, `prio` (the order
of the entries replaces it), the airport settings `timeMode` and `timezone`, and the
`arrAsDep`/`active` area settings. Area states are replaced by strategies.

## Automatic runway configuration

Configuration Auto selects existing airport custom rules from EuroScope's active
arrival/departure runways. It starts enabled unless `enabledByDefault` is false.
It is independent of SID-assignment Auto (`.vsid auto`): it never changes the
controller's active airports, runway selections or clearances.

Install **version 2** `vSidAutoConfig.json` in the Airport Config directory set by
`airportConfigs` in `vSidConfig.json`. Relative directories are resolved against
the loaded DLL, using the same path as the airport loader. Regional mappings are
maintained with the [airport configurations](https://github.com/vaccfr/vsid-configurations),
not in the plugin release. There is no fallback to a JSON beside the DLL or to
hard-coded mappings. A missing or invalid file is reported; a failed reload keeps
the last valid mappings.

```text
.vsid autoconfig on
.vsid autoconfig off
.vsid autoconfig status
.vsid autoconfig reload
.vsid autoconfig EGKK manual
.vsid autoconfig EGKK auto
.vsid autoconfig resume all
```

`ICAO auto` clears only that airport's override; `resume all` clears all overrides,
including inactive airports. Neither enables Auto globally. Use `on` explicitly.
Changing a managed rule while Auto is ON creates a manual override containing only
managed keys. Unrelated rule changes do not suspend detection. While Auto is OFF,
ordinary rule changes do not create overrides and no saved values are restored;
changes to an existing override update its managed values. The explicit `ICAO manual`
command may record an override while OFF. Status output lists retained overrides,
including inactive airports, so enabling Auto cannot hide them. Overrides last for
the current plugin session and survive airport deactivation/reactivation. Reloading
the JSON retains only saved keys that are still managed.

### Configuration format

```json
{
  "version": 2,
  "enabledByDefault": true,
  "references": {
    "EGLL": { "groups": { "east": ["09L", "09R"], "west": ["27L", "27R"] } }
  },
  "airportGroups": { "REGIONAL": ["EGKK", "EGLC"] },
  "profiles": [
    { "name": "East", "when": {"EGLL": "east"}, "enable": {"REGIONAL": ["east"]} },
    { "name": "West", "when": {"EGLL": "west"}, "enable": {"REGIONAL": ["west"]} }
  ]
}
```

The example assumes `east` and `west` already exist as custom rules at both targets.
Managed rules are the union of keys enabled for each airport across all profiles.
A matching profile turns its listed keys ON and that airport's other managed keys
OFF. `[]` turns all managed keys OFF; omitting an airport leaves it untouched.
Other rules are never changed. Airport groups are optional, contain airport codes
without nested groups, and cannot have airport-shaped four-character names. A
profile cannot address the same airport through overlapping groups/direct targets.

Reference `source` defaults to `"both"`; `"arrivals"` or `"departures"` restricts
the observation. `ignore` optionally lists runways excluded from matching. A group
matches when all selected, non-ignored runways belong to it; one parallel is enough.
Identifiers are case-insensitive. Conflicting or unknown runways, incomplete rule
sets, ambiguous profiles and missing observations retain the current rules.
The plugin warns when a group contains runways absent from the active sector file.

An optional `"fallback": true` profile may match fewer references when no normal
profile matches, for example when a secondary airport has no runways selected.
Fallbacks are explicit policy choices: none are inferred or enabled by default.
They cannot override conflicting runway observations, ambiguous normal matches,
or a valid normal match. Multiple matching fallbacks are also ambiguous.

Version 1 is rejected with a migration diagnostic: replace each `apply` object with
an `enable` array of its true keys, keep targets whose arrays become empty, and
move the file from the DLL folder to the Airport Config directory. `source` may be
omitted when it is `"both"`. Reload with `.vsid autoconfig reload`.

### Generic bridge state and commands

Bridge schema 2.0 keeps the existing `sid`, `rwy` and `cfl` aircraft fields and adds
global snapshots for all active airports:

| Field | Format | Maximum UTF-8 bytes |
| --- | --- | --- |
| `rules` | JSON object: airport to custom-rule boolean object | 65,536 |
| `strategy` | JSON object: airport to `{strategy, label, assign, source, effective}` | 65,536 |
| `autoconfig` | JSON object: airport to `{status, profile, detail, manual}` | 65,536 |
| `automode` | SID-assignment Auto, repeated `ICAO=0;` / `ICAO=1;` records | 4,096 |

`status` is one of `NOT_LOADED`, `UNMANAGED`, `MISSING_RULE`, `OFF`, `MANUAL`,
`MATCHED`, `UNDETERMINED`, `AMBIGUOUS`; `profile` is nonempty for `MATCHED`.
`manual` reports a retained override even when global Auto is OFF. JSON escapes
arbitrary rule, strategy and profile names. Consumers interpret their own regional
rules and strategies. The plugin has no Paris-specific fields or commands.

`strategy` example:

```json
{"LFPG":{"strategy":"ranux-dikol-sud","label":"RANUX DIKOL -> Sud","assign":{"LANVI":"nearest"},
 "source":"manual","effective":{"DIKOL":"south","LANVI":"nearest","RANUX":"south"}}}
```

`strategy` is `nominal` (label `Nominal`) when no strategy is active, `source` is
`manual` or `external`, and `effective` lists every point of the airport whose resolved
mode is not `default` (a `*` of the strategy is expanded per point); a point missing from
`effective` is in mode `default`. The `areas` field of schema 1.1 is removed.

Snapshots replace previous values, so inactive airports disappear and `{}` means
no active airports. Oversized snapshots are skipped with a warning, retaining the
last complete value rather than publishing an empty/truncated one. Unchanged
snapshots and failed writes do not advance the cache; failed writes can be retried.

`.vsid rules ICAO KEY=on KEY2=off ...` sets existing rules in one validated operation.
It rejects unknown/duplicate keys and invalid values before changing anything, and
observes the same managed-key override policy as `.vsid rule`. The existing singular
command keeps its toggle semantics. All commands use the normal debug logging path.

Rule changes through `.vsid rules` and Auto configuration commands invalidate
affected uncleared flights at SID-assignment Auto airports, preserving their saved
flight information so normal processing can assign SIDs under the new rules.
Other affected flights have their suggestions refreshed. Airport status lookups
are case-insensitive, and bridge snapshots use uppercase airport codes.

### Building and verification

```text
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Without Windows, `bash tests/run_linux.sh` builds and runs the EuroScope-free tests
with g++ (C++20), including the v2 parser, the SID engine, the strategies and the
generated LFPG/LFPO/LFPN/LFBO cases; `cd tools && python3 -m unittest discover -s vsid_v2/tests`
tests the v1-to-v2 converter (`python3 -m vsid_v2`).

Tests cover generic mappings, validation failures, empty enable arrays, manual
overrides, global OFF/resume, fallback selection, missing runways, generic bridge
payloads and overflow handling. Set `VSID_AUTO_CONFIG_TEST_FILE` to the separately
maintained Paris JSON to enable the additional regional mapping test. In EuroScope,
verify runway changes, manual overrides and airport reactivation with the installed
airport data; standalone tests do not exercise the host callbacks.

## Help and Support
If you need any help or support setting up the config for your airport (vACC), feel free to open an issue.
