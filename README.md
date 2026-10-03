## vSID

vSID is a powerful plugin for Euroscope that assists controllers with SID selection, clearances and some additional features.

The plugin is used within several vACCs by default.

## Feature List
* Configuration files for great variation and different conditions. For details check the Github wiki to find out the huge potential of the plugin.
* SID/RWY suggestion based on the config file and the filed flight plan.
* Initial climb suggestion based on the SID.
* Different colors for SID, climb, runway to indicate any deviations from default assigment and highlights.
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

Bridge schema 1.1 keeps the existing `sid`, `rwy` and `cfl` aircraft fields and adds
global snapshots for all active airports:

| Field | Format | Maximum UTF-8 bytes |
| --- | --- | --- |
| `rules` | JSON object: airport to custom-rule boolean object | 65,536 |
| `areas` | JSON object: airport to area boolean object | 65,536 |
| `autoconfig` | JSON object: airport to `{status, profile, detail, manual}` | 65,536 |
| `automode` | SID-assignment Auto, repeated `ICAO=0;` / `ICAO=1;` records | 4,096 |

`status` is one of `NOT_LOADED`, `UNMANAGED`, `MISSING_RULE`, `OFF`, `MANUAL`,
`MATCHED`, `UNDETERMINED`, `AMBIGUOUS`; `profile` is nonempty for `MATCHED`.
`manual` reports a retained override even when global Auto is OFF. JSON escapes
arbitrary rule, area and profile names. Consumers interpret their own regional
rules and area meanings. The plugin has no Paris-specific fields or commands.

Snapshots replace previous values, so inactive airports disappear and `{}` means
no active airports. Oversized snapshots are skipped with a warning, retaining the
last complete value rather than publishing an empty/truncated one. Unchanged
snapshots and failed writes do not advance the cache; failed writes can be retried.

`.vsid rules ICAO KEY=on KEY2=off ...` sets existing rules in one validated operation.
It rejects unknown/duplicate keys and invalid values before changing anything, and
observes the same managed-key override policy as `.vsid rule`. The existing singular
command keeps its toggle semantics. All commands use the normal debug logging path.

### Building and verification

```text
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Tests cover generic mappings, validation failures, empty enable arrays, manual
overrides, global OFF/resume, fallback selection, missing runways, generic bridge
payloads and overflow handling. Set `VSID_AUTO_CONFIG_TEST_FILE` to the separately
maintained Paris JSON to enable the additional regional mapping test. In EuroScope,
verify runway changes, manual overrides and airport reactivation with the installed
airport data; standalone tests do not exercise the host callbacks.

## Help and Support
If you need any help or support setting up the config for your airport (vACC), feel free to open an issue.
