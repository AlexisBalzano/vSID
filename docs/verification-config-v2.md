# Manual verification: SID config v2 (EuroScope, Windows)

The code was built and tested only on Linux with stubs. This checklist is the first
run against a real EuroScope. Tick each box; note the actual result when it differs.
Messages quoted below appear in the vSID chat. A strategy command prints
`[ICAO] Strategy <label> (<manual|external>), overrides: <WPT=mode,...|none>` when
queried and `[ICAO] Strategy <label> applied.` after a change.

## 0. Prerequisites

- [ ] Windows machine with EuroScope, a French sector file (LFPG, LFPO, LFPN, LFBO) and the VATSIM or a sweatbox session.
- [ ] Airport configs v2 from `vsid-configurations` (`lfpg.json`, `lfpo.json`, `lfpn.json`, `lfbo.json`) copied to the directory set by `airportConfigs` in `vSidConfig.json`.
- [ ] A v2 `vSidAutoConfig.json` for LFPN (section 5) in the same directory. The configurations repository does not contain one; use the Paris regional mapping maintained by the nav team.

## 1. Build

```text
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release
```

- [ ] Configure succeeds.
- [ ] Release build succeeds and produces `vSID.dll`.
- [ ] `ctest` passes all tests.

If the build fails: the code was only syntax-checked on Linux with stubs, so a compile
error on the first real build is plausible. Do not patch around it; copy the full
compiler message (file, line, error code) and report it, together with the MSVC and
Windows SDK versions. Same for a failing test: report the test name and output.

## 2. Install and start

- [ ] Copy `vSID.dll` (and `vSidConfig.json`) to the plugin folder, load it in EuroScope. No crash, no "config v2 rejected" message for the four airports.
- [ ] Activate LFPG as departure airport. The plugin loads without error in the chat.
- [ ] vSMR (not yet updated): its vSID integration shows as unavailable because of bridge schema 2.0. Expected until the vSMR update; not a failure.

## 3. LFPG (strategies, assign, runway groups)

Groups: north = 09L/09R/27L/27R, south = 08L/08R/26L/26R. `defaultStrategy` is `roulage-mini` (all points `nearest`). Other strategies: `ranux-dikol-sud`, `lanvi-bubli-baxir-nord`. Test with uncleared departures; select the departure runways in EuroScope so both groups are usable (e.g. 08R/08L/09L/09R or 26L/26R/27L/27R as the traffic allows).

- [ ] **Default strategy.** Right after activating LFPG, run `.vsid strategy LFPG`. Expected: `[LFPG] Strategy Roulage mini (manual), overrides: none` (source text may read `manual` by default; note what is shown).
- [ ] **Nearest behaviour.** With `roulage-mini`, a DIKOL departure positioned inside the north area gets a north-group SID/runway; one in the south area gets a south one; one outside both areas follows `selection.DIKOL` (north first).
- [ ] **ranux-dikol-sud.** `.vsid strategy LFPG ranux-dikol-sud`. Expected: `[LFPG] Strategy RANUX DIKOL -> Sud applied.` An uncleared DIKOL (and RANUX) flight that was on a north runway (27) is re-suggested on 26 (south group, SID B). Overrides are cleared.
- [ ] **Status.** `.vsid strategy LFPG` now shows `RANUX DIKOL -> Sud (manual), overrides: none`.
- [ ] **nominal.** `.vsid strategy LFPG nominal`. Expected: `[LFPG] Strategy Nominal applied.` DIKOL goes back to the `selection` order (north first, 27).
- [ ] **assign.** `.vsid assign LFPG DIKOL=north`. Expected: `[LFPG] Strategy ... applied.`; a DIKOL flight is suggested on a north runway; `.vsid strategy LFPG` lists `overrides: DIKOL=north`.
- [ ] **assign validation.** `.vsid assign LFPG XXXXX=south` and `.vsid assign LFPG DIKOL=bogus`. Expected: `[LFPG] Strategy unchanged: <reason>`; nothing changes (also not DIKOL).
- [ ] **assign clear.** `.vsid assign LFPG clear`. Overrides back to `none`.
- [ ] **Unknown strategy.** `.vsid strategy LFPG nope`. Expected: `[LFPG] Strategy unchanged: <reason>`.
- [ ] **Inactive airport.** `.vsid strategy LFBB`. Expected: `[LFBB] Strategy unchanged: inactive airport`.
- [ ] **Usage.** `.vsid strategy` alone prints the `Usage:` line.
- [ ] **ATC runway wins.** For a flight whose runway was set by the controller to a south runway (e.g. 26R) while the strategy points north: the SID offered belongs to the south group (the strategy does not override the ATC runway).
- [ ] **Cleared SID unchanged.** A flight already cleared (SID set in the route / ATC block `SID/RWY`) keeps its SID and runway after every strategy or assign change above.
- [ ] **Manual SID, engine-specific climb.** From the SID menu pick a SID manually: for LFPG a turboprop gets initial climb 5000, a jet 10000 (`defaults.initialClimb`).
- [ ] **Reload keeps state.** Activate `ranux-dikol-sud` plus `.vsid assign LFPG LANVI=nearest`, then `.vsid reload`. `.vsid strategy LFPG` still shows the same strategy and overrides.
- [ ] **Deactivate/reactivate.** Deactivate LFPG and reactivate it: the strategy and overrides are still the same (session state).
- [ ] **EQUIP.** A DIKOL/RANUX flight with a non-RNAV flight plan equipment shows `EQUIP` as SID (default requires RNAV) when no other SID is possible.

### Opposing rule (via vSMR or manual)

- [ ] `opposing` is off at start (`customRules.opposing: false`). DIKOL suggests SID A (27) / B (26), not D/K/L/E.
- [ ] Switch the rule on: through the vSMR Linked/Unlinked control if available (vSMR is not yet updated: this part is expected not to work and can be skipped), otherwise `.vsid rule LFPG opposing` (or `.vsid rules LFPG opposing=on`). Uncleared DIKOL/RANUX flights are re-suggested with the `opposing` entries first (D on 27, K on 09, E/L on 26/08). Switching it off restores A/B.

## 4. LFPO (opposing)

- [ ] LFPO loads, runways 02,20,06,24,07,25.
- [ ] `opposing` is off at start; ATREX suggests W (24) with the default rule state.
- [ ] `.vsid rules LFPO opposing=on`: ATREX flights are re-suggested with the `opposing` entries (Q on 24, H on 06, G on 07 depending on active runways). With `opposing=off` they go back to W/F/E.
- [ ] Turboprop and jet differ on H/G (climb via + 7000 for P/T, none for J) where the config says so.

## 5. LFPN (regional rules via autoconfig)

Rules: `wlpg`, `wipg`, `elpg`, `eipg` (all false at load). Runways 07L,25R,07R,25L.

- [ ] `.vsid autoconfig status` lists LFPN with a status (`MATCHED` when the reference airport has matching runways, otherwise `UNDETERMINED`, never `NOT_LOADED` once loaded).
- [ ] With the Paris references set to an east configuration the rules `elpg`/`eipg` turn ON and `wlpg`/`wipg` OFF; with west the opposite. Check with `.vsid autoconfig status` and the ATREX/NURMO suggestion (V/A for east, P/B for west).
- [ ] `.vsid autoconfig LFPN manual` then a manual rule change sticks; `.vsid autoconfig LFPN auto` returns control.
- [ ] A v1 `vSidAutoConfig.json` is rejected with a migration diagnostic (optional check).

## 6. LFBO (area airbus)

Groups: airbus = 14R/32L, other = 14L/32R. `defaultStrategy: nearest`, area `airbus` (group airbus). The conversion report notes differences to settle with the nav (`CONVERSION_REPORT.md`, section "Differences to settle with the nav"; option: remove `selection` for LFBO points).

- [ ] `.vsid strategy LFBO` shows `Nearest (manual), overrides: none`.
- [ ] A jet (J/T) positioned inside the airbus area is suggested on an airbus runway (14R/32L, SID A); the same aircraft outside the area follows `selection` (other first).
- [ ] Turboprop: engine-specific entries apply (SID H 14R/14L with climb via for P/T, B/A for J/T).
- [ ] Observe and note, for the nav: AMOLO, MEN, FINOT, LACOU, FISTO (turboprops inside the airbus area at 14: v1 H 14R, v2 A 14L or H 14L) and GAUDE, LMB (v1 A3 14R, v2 A2 14L). Record whether the v2 outcome is acceptable or the nav wants `selection` removed.

## 7. Bridge: `strategy` field and `strategySource`

- [ ] **Bridge field.** With the bridge debug tool (or another plugin reading the bridge), global field `strategy` exists and changes after each command, e.g. `{"LFPG":{"strategy":"ranux-dikol-sud","label":"RANUX DIKOL -> Sud","assign":{...},"source":"manual","effective":{"DIKOL":"south","RANUX":"south"}}}`. The field `areas` no longer exists (schema 2.0).
- [ ] **Source with a test field.** Set `"strategySource": "<provider>/<field>"` in `vSidConfig.json` (a test plugin or the bridge tool writing a field), restart/reload. Expected chat: `Strategy source <provider>/<field> connected.` (if the field is missing: `... not available on the bridge (status N)`, retried every 30 s).
- [ ] Write `{"LFPG":{"strategy":"ranux-dikol-sud","assign":{"DIKOL":"south"}}}` to the field. Expected: `[LFPG] Strategy RANUX DIKOL -> Sud applied from <provider>/<field>.` and `.vsid strategy LFPG` shows source `external`.
- [ ] Write an invalid value (unknown strategy). Expected warning `Strategy source <provider>/<field> value ignored: ...`; state unchanged.
- [ ] A manual `.vsid strategy LFPG nominal` makes the airport local: a new value in the field is ignored for LFPG.
- [ ] `.vsid strategy LFPG resume`. Expected: `[LFPG] Strategy handed back to <provider>/<field>, now ...` and the source value is applied again. Without a configured source: `[LFPG] Strategy handed back to the external source, but none is configured.`

## 8. Rejection of a v1 file

- [ ] Put a v1-format airport file (with the former `sids` array and no `"version": 2`) in the airport config directory and reload (`.vsid reload`). Expected: error message `[ICAO] config v2 rejected: <reason>` in the vSID chat; the airport is not loaded; the other airports still work.
- [ ] Remove the file and reload: no message.

## 9. Removed commands

- [ ] `.vsid area`, `.vsid lvp`, `.vsid night` / `.vsid time` and `.vsid debug area` are no longer recognised (no area debug messages).

## Reporting

Report failures with: the step, the exact chat text, the aircraft (type, engine, position), and the relevant lines from the `logs` folder next to the DLL (enable `.vsid log dev` if needed). A crash produces a `.txt` and `.dmp` in the same folder.
