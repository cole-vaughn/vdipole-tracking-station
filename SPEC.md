# Satellite V-Dipole Tracking Station: Dashboard v2 Spec

Status: interview done, analysis reviewed, build in progress (see section 13).
Last updated: 2026-10-31. This file replaces the draft PDF as the source of truth.

Rules that apply to everything below:

- Real numbers come from the station. Test fixtures are labeled on screen and in code and never mixed with real data.
- No credentials, no street address, anywhere in the repo, commits, this file, or chat.
- Free services only, no payment card. Re-check vendor limits before relying on them.
- Ask before anything that cannot be undone: deleting data, rotating keys, changing database rules, force-pushing, enabling Pages.
- No new hardware without asking. Plain words, short steps, no em dashes in anything Cole reads.
- If a feature cannot be done honestly, say so and propose the honest version.

## 1. Goal

A public, live mission-control page for the station. It shows every sensor, says honestly how fresh the data is, and ties the broadband RF reading to where a real satellite (Meteor-M2-4) is, on a 3D globe. It is a university project (ELEE 4230/6230), so the code must be tidy and explainable by a student.

Success criteria (from the draft, unchanged):

| # | Goal | How we know |
|---|---|---|
| G1 | Live | Typical data age on the page under 5 s. A touch shows in under 1.5 s. |
| G2 | Meaningful | Computed satellite position matches an independent tracker. RF visuals respond to the real reading. |
| G3 | Complete | Every field in section 6 appears in a card or graph, with a clean empty state when null. |
| G4 | Trustworthy | Stale or offline station shows a banner. No unlabeled simulated values. |
| G5 | Free | Projected usage under 50% of free limits (see section 5). |
| G6 | Good on phone and laptop | Usable at 360 px wide. Cole reviews screenshots at both sizes. |

## 2. Decisions

| ID | Decision | Reason | Source |
|---|---|---|---|
| D1 | Backend is Firebase Realtime Database on the Spark plan. The ESP32 writes, the site only reads. | Fast push updates, no field cap, no card. | Draft, kept |
| D2 | Firebase is not set up yet. Cole creates the project, database and the one ESP32 user using the firmware README steps. | The agent cannot create the account. | Q1 |
| D3 | New repo, public. The v1 repo is not reused. The agent commits locally only. Cole creates the remote, pushes, and enables Pages. | Clean history. v1 only ever held the HTML file, but a fresh start is simpler. | Q2 |
| D4 | Plain static files with pinned CDN libraries. No build tool. | Matches v1, no deploy step, easy to explain. | Q3 |
| D5 | Globe imagery is the Natural Earth set bundled with CesiumJS. No Cesium ion token, no account. Explicitly turn off every default that calls ion. | No token, no policy risk. Low detail is accepted. | Q4 |
| D6 | Charging state is inferred, never read. See section 7, F2. It is shown as "Charging (inferred)" with a hysteresis band so small spikes do not flip it. | Cole asked for an intelligent inference. The sensor probably cannot see charge current. | Q5 |
| D7 | Freshness: LIVE under 6 s, DELAYED 6 to 30 s, OFFLINE over 30 s. All in config. | Station writes every 2 s. | Q6 |
| D8 | Temperature has a C/F toggle. Times show in the viewer's local time and UTC. Data is stored in UTC and Celsius. | Friendlier for US viewers. | Q7 |
| D9 | Pressure and altitude appear only in the graphs section. The firmware keeps sending them. | Cole does not want them in the main area. | Q8 |
| D10 | A touch makes a ripple on the station marker and briefly highlights the touch card. No sound. | Cole chose the proposal. | Q9 |
| D11 | Include a minimal "next pass in HH:MM" line, computed from the same orbit data as the globe. | Gives the idle page a purpose. | Q10 |
| D12 | Graph ranges are 15 min, 1 h, 6 h, 24 h. No pruning job, no rollups, so no admin credential anywhere. Old history is deleted by hand in the Firebase console (see Risks). | Keeps v1 credential free. | Q11 |
| D13 | The old v1 page is taken down by Cole after v2 is live. No history import. Old numbers include mock values and must not mix with real data. The agent does not touch v1. | Cole's call. | Q12 |
| D14 | Station location is one config value: 33.92 N, 84.84 W (Dallas, GA, two decimals). Moving to Athens, GA later is a one-line change. | Rounded to about 1 km for privacy. | Q13 |
| D15 | Battery colors: green above 35%, amber 20% to 35%, red below 20%, grey when offline or null. Low-voltage warning under 9.5 V also turns it red. | Cole's choice. | Q14 |
| D16 | The project's old name is not used anywhere in the repo, site or docs. Page title: "Satellite V-Dipole Tracking Station". No logo. No university or course branding unless Cole asks later. | Cole's choice. | Q15 |
| D17 | Defaults for Q16, accepted unless Cole objects: sound off (there is none), reduced-motion respected (no ring pulses, blade spin, globe auto-motion), status never by color alone. | Draft proposal. | Q16 |
| D18 | Google Photorealistic 3D Tiles are out of v1. | Needs a restricted, budget-capped key. | Q17 |

## 3. The station and its data

Hardware status on 2026-10-30 (from the draft, to be confirmed): enclosure printed and partly assembled. Still open: coax through the lid, foil pad, SMA connector genders, Breadboard 1 wiring, button and jack wiring, SAWbird 5 V supply (measured 0 V), lid screws. Fan tach and final touch calibration are not done. The firmware has never been compiled or run.

So the site must handle null, missing and unverified data from day one.

| Subsystem | What the number can and cannot mean |
|---|---|
| BMP388 | Air temperature and pressure. Altitude uses a fixed 1013.25 hPa sea level, so it drifts with weather and is not true altitude. The board was physically damaged, so treat it as unconfirmed until Cole checks it. |
| Thermistor | Carries a +96.2 C legacy offset in the conversion. Show an "unverified" tag on every thermistor value and chart until Cole confirms it agrees with the BMP388. One config flag removes the tag. |
| RF chain | One broadband power number in dBm (about -75 dBm with no signal). Not a spectrum. Cannot tell a satellite from interference. May be flat until the SAWbird supply is fixed. |
| Touch pad | touched, count since boot, delay, delta, fault flag. Thresholds need re-tuning after final assembly. |
| INA219 | Voltage, current and power on the 12 V line, between the button and the buck converter. It measures what the system draws. |
| Battery estimate | An estimate. Derating 0.90 and internal resistance 0.25 ohm are placeholders. Always shown with "~". |
| Fan | RPM from the tach, 2 pulses per turn. Not wired yet, so RPM is null. RPM also falls as battery voltage falls. |
| ESP32 | WiFi RSSI and uptime. |

## 4. Repo, naming and hosting

- Names (approved): repo and folder `vdipole-tracking-station`; sketch `firmware/station_firmware/station_firmware.ino` (the Arduino IDE needs the folder name to match the sketch name). The local working folder is renamed by Cole at handoff, because renaming it mid-session breaks the agent session.
- The secret scan also fails on the old project name, so it cannot creep back in.
- The draft PDF is not committed. This file replaces it.
- Proposed layout:

```
SPEC.md  README.md  .gitignore
site/            the published website (index.html, js/, css/, data/gp.json)
tests/           unit tests, Playwright tests, tests/fixtures (never published)
firmware/        sketch, secrets.example.h, README_firmware.md
.github/workflows/   tests, secret scan, orbit data job, Pages deploy
(.claude/skills/ holds local helper skills; git-ignored, not published)
```

- Hosting (approved): GitHub Pages on a public repo, deployed from `site/` by a GitHub Actions workflow, so tests, fixtures and firmware are not published.
- Secret scan: a check that fails on a staged `secrets.h` or on strings that look like keys or passwords. It must skip `.claude/skills/` because the Playwright docs there contain sample passwords such as `pass123`.

## 5. Architecture and free-plan budget

```
ESP32 --sign in, PUT latest 2 s, POST history 10 s--> Firebase RTDB --stream + history query--> Browser (static site)
GitHub Action (every few hours) --> CelesTrak GP data --> site/data/gp.json --> Browser (same origin)
```

- Only the ESP32 writes. The site needs no key or password, only the public database URL.
- Live listener on `/station/latest` and `/station/touch` only. Never a listener on `/station/history`.
- History is a query: `orderBy="ts"` with `limitToLast` or `startAt`. Fetch once per range selection, then append live points. Downsample to about 1,500 points per series.
- Close the stream when the tab is hidden, reopen when visible. Reconnect with backoff.
- Null means no reading: cards show "--", charts show gaps.
- Page data age = now minus `ts`, corrected for the viewer's clock offset (see Open items, O3).

Budget (re-check on the vendor pages before relying on it): Spark allows 100 connections, 1 GB stored, 10 GB per month downloaded.

| Item | Draft number | Note |
|---|---|---|
| Record size | 420 bytes | My count of the firmware's JSON is likely higher. Measure it. See analysis A3. |
| History growth | about 3.6 MB/day | At 520 bytes it is about 4.5 MB/day. Both mean roughly 220 to 275 days to fill 1 GB. |
| One always-open tab | about 18 MB/day | About 0.54 GB/month. 50% of the download limit is about 9 such tabs. |
| 24 h history load | about 3.6 MB | Fine now and then, not on every page view. |

Database rules (any change needs Cole's OK; Firebase is not set up yet, so these can be right from the start). Proposed, stricter than the firmware README (see analysis A1):

```json
{
  "rules": {
    "station": {
      "latest":  { ".read": true },
      "touch":   { ".read": true },
      "meta":    { ".read": true },
      "history": {
        ".read": "query.orderByChild == 'ts' && query.limitToLast <= 9000",
        ".indexOn": ["ts"]
      },
      ".write": "auth != null && auth.token.email == '<esp32 user email>'"
    }
  }
}
```

Replace the placeholder email only in the Firebase console. Turn off new sign-ups if the console allows it.

## 6. Fields

All 25 keys under `/station/latest` (and each history record): ts, tempC, pressHPa, altM, thermRaw, thermFilt, rfRawDbm, rfFiltDbm, capTdUs, capDeltaUs, capPF, touched, touchCount, capFault, battV, battmA, battmW, battSocPct, battWhLeft, battHoursLeft, battPeakmA, fanRpm, fanStalled, rssi, uptimeS. Also `/station/touch` {state, count, ts} and `/station/meta` {fw, bootTs}.

| Where | Fields |
|---|---|
| Status bar | ts (age), uptimeS, rssi |
| Battery widget | battSocPct, battHoursLeft, battV, battmA, battmW, battWhLeft, battPeakmA (detail) |
| RF meter | rfFiltDbm, rfRawDbm (graph) |
| Fan card | fanRpm, fanStalled |
| Touch card | touched, touchCount, capFault, capDeltaUs (detail), last touch time |
| Environment | tempC, thermFilt (tagged unverified), thermRaw |
| Graphs only | pressHPa, altM, capTdUs, capDeltaUs, capPF (labeled uncalibrated) |

## 7. Features for v1

Priority order: look and feel, live data and freshness, battery widget, globe with RF visuals, touch, fan, then graphs.

**F0 Look and feel.** Mission-control dark. Starting tokens (change freely): background #0a0e14, panels #111823, lines #1f2b3a, text #d6e2f0, muted #7d8fa3, accent cyan #4fd1ff, amber #ffb454, good #5ee09a, bad #ff5c6a. Monospace tabular numerals for readings. Mobile first to 360 px, 44 px touch targets, reduced motion respected, never color alone. Local time and UTC. A C/F toggle remembered in localStorage (wrapped in try/catch).

**F1 Connection and freshness bar.** Always visible: LIVE, DELAYED or OFFLINE, data age, uptime, WiFi, last seen. OFFLINE shows a banner within 1 s of the threshold, dims cards, keeps last values and graphs. The age counter ticks every second on its own. Handles browser network loss, stream errors and reconnects, and clears itself on reconnect.

**F2 Battery widget.** Rounded outline with a nub, fill follows battSocPct, number beside it, "~Xh Ym left" ("--" when null). Colors per D15. Second line: V, mA, mW, Wh left. Labeled as an estimate. Low warning below 20% or under 9.5 V. Charging state per D6:

- Inferred in the browser from battV, battmA and battmW trends. No firmware change needed to try it.
- Idea: while the battery charges, its terminal voltage sits above what a discharging pack would show at that load and charge level, and it can rise while the load stays flat.
- Hysteresis: enter "Charging (inferred)" only after the signal holds for a set time (start at 20 s). Leave it only after the signal clearly clears for longer (start at 60 s). Minimum hold time per state. Small spikes cannot flip it. All numbers in config.
- Cannot be tuned without real data. Cole runs one logged charge (plug in for 10+ minutes, then unplug) and the thresholds are fitted to it (O4). Until then the badge is off and the widget shows discharge only.
- If the fit is not reliable, the badge is dropped. Unreliable charging claims are worse than none.
- Renders correctly at 100, 80, 50, 35, 20, 5, 0 percent and with null, no layout shift.

**F3 Globe (CesiumJS, pinned version).** Bundled Natural Earth imagery, no ion calls, no console errors from default ion requests. Station marker from config. Meteor-M2-4 position from satellite.js (SGP4) at about 1 Hz from `data/gp.json`. Ground track, optional footprint, line to the satellite when above the horizon with azimuth, elevation, range. Orbit-data age shown, warning past about 7 days. A missing or very old file shows "orbit data unavailable", never a wrong satellite. Follow and reset buttons. Pause when hidden. Lighter mode on phones. Lazy-load Cesium. Satellites come from a config list even though v1 shows one.

**F3b Next pass.** One line "next pass in HH:MM" (and maximum elevation), computed by stepping SGP4 forward over the next day. Hidden if the orbit data is unavailable.

**F4 RF visuals.** Driven by dB above a rolling baseline, not absolute dBm. Baseline is the 20th percentile of rfFiltDbm over 30 minutes, held while the signal is elevated. Level = clamp((rf minus baseline) / 12 dB, 0 to 1). All constants in config because nobody has measured a real pass. Pulsing rings from the marker (none at level 0), a beam line only while the satellite is above the horizon, and an S-meter that prints real dBm and dB above baseline plus satellite elevation. Labeled "Signal strength (broadband)" with a note that a rise can be interference. No spectrum or waterfall. Idle is normal and is not faked.

**F5 Touch.** Card with TOUCHED or idle, counter since boot, last touch time, "SENSOR FAULT" on capFault. Reacts through `/station/touch`. Ripple on the marker plus a brief card highlight (D10). Without the globe loaded, the card highlight alone.

**F6 Fan.** SVG blades, visual speed capped near 3 turns per second, real RPM always printed. States: spinning, stalled (fanStalled or under 200 RPM), not connected (null). Caption notes that speed falls with battery voltage.

**F7 Graphs.** Lower section, tabs or collapsible groups. Keep the four v1 charts (BMP388 temperature/pressure/altitude on separate axes, thermistor raw vs filtered, RF raw vs filtered, BMP388 vs thermistor). Add battery, fan, capacitive delta, RSSI. Ranges 15 min, 1 h, 6 h, 24 h. Gaps for null. Chart.js, pinned.

**F8 Reliability.** No uncaught exceptions. Calm retry messages with backoff. Hidden diagnostics panel (connection state, data age, orbit-data age, last error).

Out of scope for v1: replay slider, sky plot or pass planner (beyond the one-line next pass), alerts, CSV export, accounts, controlling the station, any simulated "demo mode" shipped to visitors, more than one satellite in the UI, photorealistic tiles, history import, rollups, pruning job.

## 8. Satellite

Target: Meteor-M No.2-4, NORAD 59051, LRPT near 137.9 MHz (137.1 also listed). Status checked 2026-10-30 from SatNOGS: operational, fresh orbit data, no decoded data in the past 30 days (probably few observers, not confirmed). Cole should confirm with his own receiver. No other satellite is added without evidence.

Orbit pipeline: the Pages deploy workflow runs on every push and every 6 hours. Each run downloads the TLE for 59051 from CelesTrak, writes `site/data/gp.json` into the deploy (no commit, so no history noise), and falls back to the committed copy if CelesTrak fails. The page loads it from its own origin. SGP4 wrapper unit-tested against a known vector and cross-checked against an independent tracker at three times. No credential is needed, only the Action's own write permission.

## 9. Privacy and security

- Station location is rounded (D14). No street address anywhere.
- Everything in the repo is public (Pages needs it).
- `secrets.h` is git-ignored. Secret scan in CI (section 4).
- Database: public read of the live nodes, history read only through an indexed, limited query, single-user write (section 5).
- Firmware TLS currently skips certificate checks. Pin the Google root before treating the uplink as secure. Firmware task, Cole decides when.

## 10. Milestones

| # | Milestone | Result |
|---|---|---|
| M0 | Repo and decisions | Names approved and applied, layout, .gitignore, secret scan, README skeleton. |
| M1 | Live data and freshness | Stream, status bar, banner, null handling. Built and tested against labeled fixtures. Checked on the real database once Firebase and the firmware exist. |
| M2 | Layout and theme | Dark theme, grid, battery, fan, touch cards, C/F toggle. |
| M3 | Graphs | History queries, range selector, charts, downsampling. |
| M4 | Orbit pipeline | Action, `gp.json`, SGP4 module with tests, age warnings, next pass. |
| M5 | Globe and RF visuals | Cesium scene, track, beam, rings, S-meter, baseline logic with tests. |
| M6 | Polish | Phone pass, reduced motion, accessibility, performance, diagnostics. |
| M7 | Verification and handoff | Full test run, screenshots, final report, setup docs. |

Tests: Playwright smoke tests (Chromium, desktop and 360 px) for no console errors, OFFLINE banner on an old timestamp, battery states, fan states, a touch event, charts with gaps, globe canvas present. Unit tests for age function, baseline and level function, charging hysteresis, SGP4 wrapper, downsampling. Fixtures live in `tests/fixtures`, are labeled, and are never published. One read-only live check against the real database when it has data.

Definition of done: all milestones complete, all tests pass and shown to Cole, no console errors or warnings on a clean load, no credentials or street address in the repo or history, the site works with null, missing and stale data, README covers setup, deploy and adding a satellite, and a final report lists what was verified and what only Cole can verify on hardware.

## 11. Open items

| ID | Item | Owner |
|---|---|---|
| O1 | Create the Firebase project, database and ESP32 user (firmware README steps 1 to 3). Tell the agent only the database host name. | Cole |
| O2 | Names and Pages deploy approved 2026-10-31. Rename the local folder at handoff. | Cole |
| O3 | Decided: Firebase JS SDK from a pinned CDN (server clock offset via `.info/serverTimeOffset`, built-in reconnect). | Done |
| O4 | Logged charge test for the charging inference (section 7, F2). | Cole |
| O5 | Compile and flash the firmware, run the README checklist. | Cole |
| O6 | Bench check: thermistor vs BMP388, BMP388 health, fan tach (meter check first). | Cole |
| O7 | Firmware changes the agent wants to ask for (none made yet): rename user-visible old-name strings (splash text, version string, flash namespace), charge-aware battery estimate, slimmer history records, TLS pinning. | Cole decides |
| O8 | Take down v1 after v2 is live. Consider exporting ThingSpeak data first. | Cole |

## 12. Analysis findings

These come from reading the draft PDF and the firmware side by side. Cole reacts before tasks are planned.

A1. **Public read of the whole `/station` node is a quota risk.** The firmware README rules set `.read: true` on `/station`, which also exposes all of `history`. Anyone could download it in a loop. At about 4 MB per full pull, roughly 2,500 pulls would use the 10 GB monthly download limit. Fix: the stricter rules in section 5 (live nodes open, history only through a limited, indexed query). This needs Cole's approval since it changes database rules.

A2. **Cesium defaults call Cesium ion.** A default Viewer asks ion for imagery, which needs a token and logs errors, and that conflicts with "no console errors" and "no token". The viewer must be built with its own bundled imagery and the ion-backed widgets turned off. Loading Cesium from a CDN also needs its base URL set so the bundled imagery and workers are found. Verify in M5.

A3. **Record size is probably larger than 420 bytes.** Counting the 25 key names plus values in the firmware's JSON builder gives roughly 500 to 560 bytes. Storage and download math scales with it. Measure one real record on first data and update section 5. A slimmer history record (fewer fields) is a possible firmware change.

A4. **Critical path is Cole's, not the site's.** No live data exists until the firmware compiles, the board is flashed, and Firebase exists. RF will be flat until the SAWbird supply works. Mitigation: build and test M1 to M3 against labeled fixtures, then verify live when ready. Screenshots made before live data will be marked as fixture data.

A5. **A shipped demo mode is out of scope, but Cole still needs to see the page.** Resolved (approved): `npm run dev:fixtures` serves the site with the data source swapped for labeled fixtures from `tests/`. It is never published and says "TEST FIXTURE DATA" on screen.

A6. **Battery estimate ignores charging.** The firmware only subtracts energy, with a slow voltage nudge near full and near empty. While charging, the percent will lag or be wrong. The site should not present it as exact during an inferred charge. A charge-aware firmware estimate is the clean fix (O7).

A7. **Charging inference may not work.** It depends on the charge jack wiring, which is unfinished. The plan therefore needs a real capture (O4) and a rule to drop the badge if it is unreliable.

A8. **Scheduled Actions can be paused by GitHub.** Public repos can have scheduled workflows disabled after a long period without repo activity. Resolved: the orbit fetch is part of the deploy workflow (no bot commits, which also avoids the rule that bot pushes do not trigger other workflows), the page warns when orbit data is over 7 days old, and the README says how to re-enable the schedule.

A9. **Draft contradictions fixed here:** the draft's red-at-20% battery rule is replaced by D15 (amber band); the draft's "keep the four thermistor charts" and "trust only after bench check" are reconciled with the unverified tag; the draft names the project in many places, replaced by D16.

A10. **Renaming touches the firmware.** The sketch file, its version string, the LCD splash text, the README title, and the flash namespace used for the saved battery value all carry the old name. Renaming the flash namespace makes the board forget its saved battery value once (it restarts from the voltage reading; press `f` after a full charge). The Arduino IDE also needs the sketch inside a folder of the same name. None of this is done yet.

A11. **Two TLS clients on an ESP32 can exhaust heap.** The firmware keeps two secure clients alive. This is a firmware risk, not a site risk, noted for the compile-and-run step.

A12. **Thermistor offset of +96.2 C.** Without the unverified tag, thermistor charts could be read as truth. Tag stays until Cole clears it.

## 13. Task list

Pause points: one local commit per milestone, so progress is saved even if a session ends.

| Task | Milestone | Acceptance | State |
|---|---|---|---|
| Rename firmware files and strings, firmware README rules | M0 | Secret scan finds no old name | Done |
| .gitignore, secret scan, pre-commit hook, package.json | M0 | `npm run scan` passes; hook blocks a staged secrets.h | Done |
| Config, data source (SDK), fixture source and dev server | M1 | Site runs with fixtures; empty config shows a calm "not connected" state | |
| Status bar, freshness, banner, reconnect | M1 | Banner within 1 s of OFFLINE; clears on fresh data | |
| Theme, grid, battery, fan, touch, RF meter, C/F toggle | M2 | Battery states 100 to null render with no layout shift | |
| Charging inference module (badge off until tuned) | M2 | Unit tests: spikes do not flip state | |
| Graphs, history query, downsampling, live append | M3 | Charts show gaps for null; under 1,500 points per series | |
| Orbit module, gp.json, next pass, deploy workflow | M4 | SGP4 matches the reference vector | |
| Globe, track, beam, rings, touch ripple | M5 | Canvas renders, no console errors | |
| RF baseline and level module | M5 | Unit tests for baseline hold during a pass | |
| Mobile, reduced motion, diagnostics | M6 | 360 px usable; reduced motion stops animation | |
| Full test run, screenshots, README, handoff list | M7 | All tests pass | |
