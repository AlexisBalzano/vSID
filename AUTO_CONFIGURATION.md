# Automatic runway configuration

Select existing airport custom rules automatically from EuroScope's active runways.
The supplied `vSidAutoConfig.json` contains editable Paris mappings. Airport SID
configuration files must be installed separately; none are bundled by this change.

Auto configuration starts ON by default. To check it or enable it again:

```text
.vsid autoconfig on
.vsid autoconfig status
```

The `enabledByDefault` setting in `vSidAutoConfig.json` controls the startup state
(default `true` when omitted). Set it to `false` to start OFF next time the plugin
loads. `.vsid autoconfig off` still disables it for the current session. Reloading
the JSON preserves the current ON/OFF selection. It is independent of
`.vsid auto`, which controls automatic SID assignment. Configuration detection
does not enable SID assignment, grant clearances, or change EuroScope's active
airports or runway selections. No AVISO window is required.

Changing EuroScope's airport/runway selections and confirming the dialog
automatically re-evaluates the active airports. A newly activated airport is
included; an inactive airport is not modified. LFPG/LFPO runway selections can
be observed as references without activating either airport for vSID processing.

## Manual control

```text
.vsid autoconfig LFPN manual
.vsid rule LFPN wipg
.vsid autoconfig LFPN auto
.vsid autoconfig off
```

- `ICAO manual` freezes that airport's current rules.
- Changing an existing rule with `.vsid rule` also creates a manual override for
  that airport. The normal rule command still toggles rules; it does not become
  an exclusive configuration selector.
- `ICAO auto` removes its override, enables the global detector, and re-evaluates
  all active airports that do not have manual overrides.
- Global `on` preserves manual overrides; global `off` retains the current rules.
- `.vsid autoconfig ICAO` displays the airport's status and rule values.
- Manual overrides survive runway changes and airport deactivation/re-activation
  in the same plugin session. Overrides and the Auto switch reset on plugin reload.

## Editing the mappings

All airport names, runway groups, ignored runways, conditions, rule names, and
rule values are defined in **`vSidAutoConfig.json` beside the loaded `vSID.dll`**.
The source copy is at the repository root. For the exported build, edit
`build/Release/vSidAutoConfig.json`. Airport SID JSON files and `vSidConfig.json`
do not need changes. There are no built-in airport mappings in the C++ detector.

After saving the file, apply it without restarting EuroScope:

```text
.vsid autoconfig reload
```

Reload preserves Auto ON/OFF and manual overrides. When Auto is ON, a successful
reload immediately re-evaluates active airports and refreshes suggestions. When
Auto is OFF, reload only replaces the mappings for the next enable. A missing or
invalid file produces an error and leaves the last successfully loaded mappings
in use. If no valid file has ever been loaded, automatic selection is unavailable;
there is no hard-coded fallback.

The JSON has these top-level fields:

- `version`: the schema version, currently `1`.
- `enabledByDefault`: startup preference, `true` by default; only applied on the
  first successful configuration load in each plugin session.
- `references`: airports whose runway selections are used as inputs. Each has
  `source` (`"both"`, `"arrivals"`, or `"departures"`), a `groups` object mapping
  your group names to runway arrays, and an optional `ignore` array.
- `profiles`: named configurations with `when` conditions and `apply` actions.
  Every `when` condition must match. `apply` lists target airports and their
  existing vSID custom-rule names with explicit `true`/`false` values.

For example, the supplied West Unlinked profile contains:

```json
{
  "name": "West Unlinked",
  "when": {"LFPG": "west", "LFPO": "east"},
  "apply": {
    "LFPG": {"opposing": true},
    "LFPO": {"opposing": true},
    "LFPN": {"wlpg": false, "wipg": true, "elpg": false, "eipg": false}
  }
}
```

This is one profile entry, not a complete replacement file. To add another
region, add its reference airports/groups and new profiles; no rebuild is needed.
Airports, groups, and rule identifiers are case-insensitive. Runways must be
strings with two digits and an optional L/C/R suffix (e.g. `"07"`, `"26L"`).

A group matches when at least one selected, non-ignored runway belongs to it and
all other non-ignored runways in the selected source belong to that same group.
One active parallel is sufficient. Ignored runways do not establish a group by
themselves. Unknown or conflicting runways leave the reference undetermined.

Every profile addressing a particular target airport must explicitly set the
same rule keys, including false values to disable alternatives. Other airport
rules are untouched. All managed keys must already exist in the airport config;
the detector never creates new rule keys. If two profiles match the same target,
the status is AMBIGUOUS and its current rules are retained. A potentially matching
profile with missing reference data also prevents guessing. Profile list order
does not resolve conflicts.

Validation rejects duplicate keys, unknown fields, missing references/groups,
overlapping runway groups, wrong types, and inconsistent target-rule sets.

## Supplied Paris mappings

| LFPG | LFPO | State | LFPG / LFPO / LFPB `opposing` | LFPN / LFPT / LFPV |
|---|---|---|---|---|
| West | West | West Linked | OFF | `wlpg` |
| West | East | West Unlinked | ON | `wipg` |
| East | East | East Linked | OFF | `elpg` |
| East | West | East Unlinked | ON | `eipg` |

LFPG east: 08L/08R/09L/09R. LFPG west: 26L/26R/27L/27R.
LFPO east: 06/07. LFPO west: 24/25. A single selected parallel is sufficient.
Arrival and departure selections are both considered; conflicting directions
produce UNDETERMINED. Orly 02/20 alone cannot identify a direction, but may coexist
with an unambiguous 06/07 or 24/25 selection.

LFOB `pgeast` follows LFPG's direction and does not need LFPO runway data.
LFPM and other airports have no supplied regional mapping and are left alone
unless you add profiles for them.
Only existing rule keys are used. Unsupported or incomplete rule sets are reported
and retained. An older LFPO `lfpg_wl/lfpg_wi/lfpg_el/lfpg_ei` schema is not
assumed equivalent to the French repository's `opposing` rule.

Missing, unexpected, or conflicting reference runway selections retain the last
rules and report UNDETERMINED. The detector does not guess a configuration or
clear existing rules when reference data disappears. The next unambiguous update
resumes detection automatically unless there is a manual override.

## Files and building

This change adds only `vSidAutoConfig.json` as configuration data. Existing
airport files and `vSidConfig.json` remain unchanged. The supplied Paris mappings
require matching existing custom rules in your installed airport configurations,
such as those in vaccfr/vsid-configurations. Missing rules are reported and retained.

Build and run the standalone detection and bridge-contract tests:

```text
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The runtime-files target copies `vSidAutoConfig.json` and this guide beside the
DLL, even when only the JSON changes. The release workflow includes both in
`files.zip`. Preserve your deployed mappings when installing later builds and
keep using your existing airport configuration directory and sector-file paths.

## EuroScope acceptance check

### vSMR live configuration display

The companion build publishes bridge schema 1.4. `vsid/paris` contains the
effective runtime Paris rules, including automatic and manual selections;
`vsid/lfpg_taxi` contains the actual LFPG area state. `vsid/automode` continues
to mean SID-assignment Auto, independently of configuration Auto.

The updated vSMR Runtime uses these values to highlight Linked/Unlinked,
regional profiles, and Minimum Taxiing/Ground Crossing. No bridge DLL update
or bridge configuration change is required. Publication runs after automatic
selection, manual rule/area commands, and on the timer, even without aircraft.
Unchanged snapshots are not republished.

NORTH and SOUTH both enabled means Minimum Taxiing; both disabled means
Ground Crossing. Missing or mixed area states, or additional active areas,
leave both taxi buttons unselected. Runway configuration Auto does not change
taxi areas. Older vSID providers retain the previous vSMR fallback showing the
last completed taxi command, identified as such in the tooltip.

The existing vSMR companion commands `.vsid paris ICAO linked|unlinked` and
`.vsid paris ICAO wlpg|elpg|wipg|eipg` select existing runtime rules explicitly.
They suspend configuration Auto for that airport, just like `.vsid rule`.
Missing required rules are rejected without creating rules or modifying files.
Use `.vsid autoconfig ICAO auto` to resume automatic selection.

After installing both DLLs and restarting EuroScope, open vSMR's vSID panel:
verify Auto changes highlight the matching rules, then change LFPG areas and
verify the taxi highlight follows them. Switch/deactivate airports and verify
the panel follows their current state. Automated tests cover the publisher's
wire values and production vSMR polling; the actual EuroScope repaint needs
this host acceptance check.

### Runway configuration checks

1. Load the DLL and Auto JSON with separately installed airport configurations and current French sector data.
2. Activate LFPG, LFPO and LFPN; select LFPG west and LFPO east.
3. Enable Auto configuration and check `opposing=ON` at LFPG/LFPO and only
   `WIPG=ON` at LFPN. Repeat for the other three table rows.
4. Change a rule manually, then change runway selections; the overridden airport
   must stay manual while other airports follow the new state.
5. Deactivate/reactivate an airport and verify its manual override survives.
6. Select conflicting directions or remove reference runway selections; check
   UNDETERMINED and retained rules. Restore valid selections and check recovery.
7. Verify manually assigned runways and existing clearances remain under your
   control. Automated tests do not replace this live EuroScope integration check.
8. Change a profile name or mapping in the deployed `vSidAutoConfig.json`, run
   `.vsid autoconfig reload`, and check that the new mapping is used. Verify that
   a malformed edit reports an error and preserves the last valid mappings.
