# Satellite V-Dipole Tracking Station: Dashboard v2 Spec

Status: v1 site built (section 13). Battery state of health (section 14) is designed and waiting for Cole's approval.
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
| D4 | Plain static files, no build step for the site. Firebase SDK, Chart.js and fonts come from pinned CDNs; the globe libraries (three.js, globe.gl, satellite.js) and Earth textures are self-hosted in `site/vendor/` and `site/textures/`, bundled once by `npm run vendor` and committed. | Matches v1, easy to explain; self-hosting removes the biggest runtime dependency on third-party CDNs. | Q3, extended |
| D5 | Globe (changed 2026-10-31, option A of three prototypes): three.js through globe.gl with NASA Blue Marble by day and NASA Earth at Night by night, blended along the real sunrise line. No token, no account. Replaces Cesium, which looked soft (half-resolution rendering) and glitchy. | Cole picked it after comparing three working prototypes. | Q4, revised |
| D6 | Charging state is inferred, never read. See section 7, F2. It is shown as "Charging (inferred)" with a hysteresis band so small spikes do not flip it. | Cole asked for an intelligent inference. The sensor probably cannot see charge current. | Q5 |
| D7 | Freshness: LIVE under 6 s, DELAYED 6 to 30 s, OFFLINE over 30 s. All in config. | Station writes every 2 s. | Q6 |
| D8 | Temperature has a C/F toggle. Times show in the viewer's local time and UTC. Data is stored in UTC and Celsius. | Friendlier for US viewers. | Q7 |
| D9 | Pressure and altitude appear only in the graphs section. The firmware keeps sending them. | Cole does not want them in the main area. | Q8 |
| D10 | A touch makes a ripple on the station marker and briefly highlights the touch card. No sound. | Cole chose the proposal. | Q9 |
| D11 | Show "next pass in HH:MM" and a table of the next 5 passes (day, start, end, length, highest elevation, with passes under 15 degrees marked low), computed from the same orbit data as the globe. | Gives the idle page a purpose. The table was added 2026-10-31 at Cole's request. | Q10, extended |
| D12 | Graph ranges are 15 min, 1 h, 6 h, 24 h. No pruning job, no rollups, so no admin credential anywhere. Old history is deleted by hand in the Firebase console (see Risks). | Keeps v1 credential free. | Q11 |
| D13 | The old v1 page is taken down by Cole after v2 is live. No history import. Old numbers include mock values and must not mix with real data. The agent does not touch v1. | Cole's call. | Q12 |
| D14 | Station location is one config value: 33.92 N, 84.84 W (Dallas, GA, two decimals). Moving to Athens, GA later is a one-line change. | Rounded to about 1 km for privacy. | Q13 |
| D15 | Battery colors: green above 35%, amber 20% to 35%, red below 20%, grey when offline or null. Low-voltage warning under 9.5 V also turns it red. | Cole's choice. | Q14 |
| D16 | The project's old name is not used anywhere in the repo, site or docs. Page title: "Satellite V-Dipole Tracking Station". No logo. No university or course branding unless Cole asks later. | Cole's choice. | Q15 |
| D17 | Defaults for Q16, accepted unless Cole objects: sound off (there is none), reduced-motion respected (no ring pulses, blade spin, globe auto-motion), status never by color alone. | Draft proposal. | Q16 |
| D18 | Google Photorealistic 3D Tiles are out of v1. | Needs a restricted, budget-capped key. | Q17 |
| D19 | The satellite is an illustrative 3D model (gold bus, two solar wings, Earth-facing antenna), drawn about 500 km across and labeled as not to scale. | Cole asked for a model instead of a dot; no exact public model exists. | Review |
| D20 | No LIVE/DELAYED/OFFLINE pill. Freshness shows as data age, the offline banner and dimmed cards. The header is not sticky on phones. | Cole's request. | Review |
| D21 | Battery state of health (SOH) is the course "go beyond" algorithm: usable energy over the 26.6 Wh label, learned from normal use (no learning runs). Each discharge is one slice of evidence; a weighted total least squares fit (Plett 2011) gives one score with a plus-or-minus, shown from the first usable slice. Separate `health` and `healthLog` nodes. No internal resistance shown. Colors 80 / 60, with 60 marked as a project choice. Full design in section 14. | Course requirement; Cole wants no special learning procedure and no new hardware. | SOH brief and Cole's answers 2026-10-31 |

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
firmware/        README.md, station_firmware/ (sketch, soh.h, secrets.example.h)
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

| Item | Number | Note |
|---|---|---|
| Record size | 417 bytes of JSON, about 441 stored with its push key | Measured 2026-10-31 by building the firmware's JSON with realistic values (13-digit `ts`, every field at its sent precision). Re-check on the first real record. |
| History growth | about 3.8 MB/day | 1 GB lasts about 262 days. |
| One always-open tab | about 18 MB/day | About 0.54 GB/month. 50% of the download limit is about 9 such tabs. |
| 24 h history load | about 3.8 MB | Fine now and then, not on every page view. |
| Battery health (section 14.13) | under 0.4 MB/day per tab, about 0.01 MB/day stored | With the recommended separate health nodes. Adding the fields to every record instead costs much more (14.13). |

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
      "health":    { ".read": true },
      "healthLog": {
        ".read": "query.orderByChild == 'ts' && query.limitToLast <= 500",
        ".indexOn": ["ts"]
      },
      ".write": "auth != null && auth.token.email == '<esp32 user email>'"
    }
  }
}
```

Replace the placeholder email only in the Firebase console. Turn off new sign-ups if the console allows it. The `health` and `healthLog` entries are for battery health (section 14), approved by Cole on 2026-10-31 (O9).

## 6. Fields

All 25 keys under `/station/latest` (and each history record): ts, tempC, pressHPa, altM, thermRaw, thermFilt, rfRawDbm, rfFiltDbm, capTdUs, capDeltaUs, capPF, touched, touchCount, capFault, battV, battmA, battmW, battSocPct, battWhLeft, battHoursLeft, battPeakmA, fanRpm, fanStalled, rssi, uptimeS. Also `/station/touch` {state, count, ts} and `/station/meta` {fw, bootTs}. Battery health adds `/station/health` and `/station/healthLog` (section 14.12).

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

**F1 Connection and freshness bar.** Always visible (sticky on wide screens): data age, uptime, WiFi, last seen (no status pill, D20). OFFLINE shows a banner within 1 s of the threshold, dims cards, keeps last values and graphs. The age counter ticks every second on its own. Handles browser network loss, stream errors and reconnects, and clears itself on reconnect.

**F2 Battery widget.** Rounded outline with a nub, fill follows battSocPct, number beside it, "~Xh Ym left" ("--" when null). Colors per D15. Second line: V, mA, mW, Wh left. Labeled as an estimate. Low warning below 20% or under 9.5 V. Charging state per D6:

- Inferred in the browser from battV, battmA and battmW trends. No firmware change needed to try it.
- Idea: while the battery charges, its terminal voltage sits above what a discharging pack would show at that load and charge level, and it can rise while the load stays flat.
- Hysteresis: enter "Charging (inferred)" only after the signal holds for a set time (start at 20 s). Leave it only after the signal clearly clears for longer (start at 60 s). Minimum hold time per state. Small spikes cannot flip it. All numbers in config.
- Cannot be tuned without real data. Cole runs one logged charge (plug in for 10+ minutes, then unplug) and the thresholds are fitted to it (O4). Until then the badge is off and the widget shows discharge only.
- If the fit is not reliable, the badge is dropped. Unreliable charging claims are worse than none.
- Renders correctly at 100, 80, 50, 35, 20, 5, 0 percent and with null, no layout shift.

**F3 Globe (three.js via globe.gl, pinned).** NASA Blue Marble and Earth at Night textures with real day and night shading. Station marker from config. Meteor-M2-4 as an illustrative 3D model (D19), position from satellite.js (SGP4) recomputed 10 times a second from `data/gp.json`. Past and upcoming ground track, a beam to the satellite when it is above the horizon, with azimuth, elevation and range. Orbit-data age shown, warning past about 7 days. A missing or very old file shows "orbit data unavailable", never a wrong satellite. Follow and reset buttons. Pauses when the tab is hidden. Lower pixel ratio on phones. Loads after the first paint. Satellites come from a config list even though v1 shows one.

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
| O9 | Decided: separate `/station/health` and `/station/healthLog` nodes, with their two read rules (section 5). | Done |
| O10 | Decided: no new hardware. Internal resistance is not measured and not shown. | Done |
| O11 | Default applied: the BMP388 air temperature gates slices (15 to 35 C). Cole can switch to the thermistor once it is verified. | Cole may change |
| O12 | Decided: 80 / 60 bands; 80 cited (USABC), 60 marked as a project choice. | Done |
| O13 | Hardware checks for SOH: INA219 calibration, charger full voltage, a few normal charge cycles (section 14.18). | Cole |
| O14 | Approve the SOH plan (section 14.19) before any code. | Cole |

## 12. Analysis findings

These come from reading the draft PDF and the firmware side by side. Cole reacts before tasks are planned.

A1. **Public read of the whole `/station` node is a quota risk.** The firmware README rules set `.read: true` on `/station`, which also exposes all of `history`. Anyone could download it in a loop. At about 4 MB per full pull, roughly 2,500 pulls would use the 10 GB monthly download limit. Fix: the stricter rules in section 5 (live nodes open, history only through a limited, indexed query). This needs Cole's approval since it changes database rules.

A2. **(Resolved by D5: Cesium removed.)** Cesium defaults call Cesium ion. A default Viewer asks ion for imagery, which needs a token and logs errors, and that conflicts with "no console errors" and "no token". The viewer must be built with its own bundled imagery and the ion-backed widgets turned off. Loading Cesium from a CDN also needs its base URL set so the bundled imagery and workers are found. Verify in M5.

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
| Config, data source (SDK), fixture source and dev server | M1 | Site runs with fixtures; empty config shows a calm "not connected" state || Done |
| Status bar, freshness, banner, reconnect | M1 | Banner within 1 s of OFFLINE; clears on fresh data || Done |
| Theme, grid, battery, fan, touch, RF meter, C/F toggle | M2 | Battery states 100 to null render with no layout shift || Done |
| Charging inference module (badge off until tuned) | M2 | Unit tests: spikes do not flip state || Done |
| Graphs, history query, downsampling, live append | M3 | Charts show gaps for null; under 1,500 points per series || Done |
| Orbit module, gp.json, next pass, deploy workflow | M4 | SGP4 matches the reference vector || Done |
| Globe, track, beam, rings, touch ripple | M5 | Canvas renders, no console errors || Done |
| RF baseline and level module | M5 | Unit tests for baseline hold during a pass || Done |
| Mobile, reduced motion, diagnostics | M6 | 360 px usable; reduced motion stops animation || Done |
| Full test run, screenshots, README, handoff list | M7 | All tests pass || Done |

Built differently from the draft, on purpose:

- Orbit data is fetched inside the deploy workflow instead of committed by a bot (see A8).
- The history read limit in the rules is 9,000 records (24 h at one per 10 s, plus margin).
- Headless test browsers log a "GPU stall due to ReadPixels" driver notice from their software GPU. The page never calls
  readPixels (checked with a probe), so the console test ignores that one notice and nothing else.

## 14. Battery state of health (the course "go beyond" algorithm)

Status: designed 2026-10-31 with Cole's answers (O9 to O12). Nothing is built yet. Waiting for approval of the plan in 14.19.

### 14.1 What it is, and how it meets the course

SOH here means: the energy the pack can deliver now, divided by the label energy (26.6 Wh), times 100. A new pack can read above 100 if the label is modest. The raw number is kept and logged; the website shows at most 100.

There are no learning runs and no special procedure (Cole's choice). The station learns from normal use: every stretch of discharge is one "slice" of evidence, and one fit turns all the slices into a single score with an honest plus-or-minus.

| Course requirement | How this design meets it |
|---|---|
| Correlates to published research | The combination step is the weighted total least squares capacity estimate of Plett 2011, built for partial use (hybrid cars that never fully discharge). SOH as releasable capacity follows Ng et al. 2009. The 80% line follows the USABC manual. See 14.17. |
| Combines measurements into a final score | Each slice pairs two measurements: energy from the INA219 and the change in charge level from the voltage. One weighted fit over all slices gives the score and its uncertainty (14.7). Temperature gates which slices count. |
| Logged to the cloud for a sufficient time | Every slice, accepted or rejected with its reason, is appended to `/station/healthLog` with the fit after it. Never pruned. |

### 14.2 What the station can and cannot measure

- **The INA219 sees only what the station draws.** It sits between the power button and the buck converter, so it never sees charger current. Charging can only be inferred: the voltage rises while the load stays steady.
- **The pack never rests while the station is on.** It always draws about 100 mA (about C/24). At that small, steady load, the voltage plus I x R is close to the open-circuit value. Points like that are "quasi-rest" anchors.
- **Small use gives a rough answer.** One hour uses about 1.2 Wh, about 4.5% of the pack, so the voltage moves only a little, and the voltage table is generic (not measured on this pack). Expected accuracy is in 14.8. Cole's usage (on battery, recharged when low) is the best case: each discharge is a long slice.
- **No internal resistance.** Measuring it needs a load step the firmware controls, and the station has none (the fan is always on; WiFi bursts are milliseconds and about 25 mA at the pack). Cole chose no new hardware, so resistance is not measured and not shown anywhere. The 0.25 ohm placeholder stays only inside the voltage correction.
- **Temperature is the box air.** Nothing touches the pack, but at 100 mA it heats itself by only about 3 mW (0.1 A squared times 0.25 ohm), so it sits at the air temperature. The BMP388 is used (default; Cole did not pick a sensor). The thermistor can replace it with one setting once verified.
- **The INA219 is uncalibrated.** `INA219_CURRENT_GAIN` (1.0) and `INA219_CURRENT_OFFSET_MA` (0.0) are placeholders. Every energy number scales with the gain. The website says "uncalibrated" until they are set.

### 14.3 Inputs, cleaned up first

- **INA219 averaging (firmware change).** Today one 0.5 ms snapshot is taken every 2 s. It catches WiFi bursts at random, which looks like about plus or minus 25% current noise and would fail every "steady current" check. New: the chip averages 128 samples per reading (69 ms), the firmware reads it every 100 ms and averages each 2 s window. The existing battery estimate gets the cleaner numbers too.
- **Open-circuit estimate:** V_oc = V_batt + I x 0.25 ohm (placeholder).
- **SOC** from V_oc with the existing table.
- **SOE (energy percent)** from the same table: voltage integrated over charge, because the top of the curve, at higher voltage, holds more watt-hours per percent. Slices use SOE so they are scaled by energy, not charge.

### 14.4 Energy counting

- E (Wh) = sum of V_batt x I x dt. Also Ah = sum of I x dt, logged so results can be compared with papers that use Ah.
- Sums use double precision. A float would lose about 0.2% over a 21-hour discharge.
- dt is capped at 10 s. A slice with more than 1% of its time lost to gaps is rejected.

### 14.5 Anchors and slices

**Anchors** are moments when the voltage says something trustworthy about the charge level:

| Anchor | When | SOE and its uncertainty |
|---|---|---|
| Full | The charger held the pack full (V_oc at or above 12.54 V, 4.18 V per cell) and was then unplugged. The last full moment is the anchor. | 100, plus or minus 1 point (the charger defines full better than the table) |
| Quasi-rest | Every 2 s window where: current under 300 mA; current steady over 2 minutes (within 5% or 8 mA, whichever is larger); voltage not rising (at most +2 mV per minute) and not collapsing (falling slower than 30 mV per minute); at least 60 minutes after a charge ended (the voltage needs that long to settle); temperature known. | From the table. Uncertainty = voltage uncertainty / slope of the curve there. Voltage uncertainty: 15 mV per cell for the generic table (PLACEHOLDER) plus I x 0.1 ohm for the resistance guess. |
| Cutoff | Battery voltage at or below 9.5 V for 30 s. | From the table (about 1), small uncertainty because the curve is steep there |

**A stretch** is one continuous discharge: from a charge ending (or a boot) to the next charge starting (or power off). Charger plugged in is detected when V_oc rises 60 mV above the stretch's lowest value for 60 s.

**One slice per stretch.** Start = the first anchor of the stretch (a full anchor if it began from a full charge). End = the anchor that gives the smallest relative error, usually the latest. Slice values: energy E between the two anchors, x = SOE_start - SOE_end, and their uncertainties.

- The open slice (the stretch in progress) updates live, so the estimate improves during a discharge instead of only at the end.
- One slice per stretch, not many small ones, because neighbouring small slices would share anchors and their errors, and the fit would count the same evidence twice.
- A slice counts only if x is at least 2 points, E at least 0.2 Wh, mean temperature 15 to 35 C, and gaps under 1%. Otherwise it is logged as rejected with the reason.
- Power off or reboot: the open slice is saved every 5 minutes together with its last good end anchor (energy and SOE from the same moment), so the next boot closes it there without losing accuracy. Energy after that last saved anchor is simply not part of the slice.

### 14.6 How sure each slice is

- sigma_x = sqrt(sigma_start^2 + sigma_end^2) from the anchors (14.5).
- sigma_E = 2% of E plus 0.02 Wh (PLACEHOLDER: shunt tolerance and INA219 gain, until calibrated).
- Anchors on the steep parts of the curve (near full and near empty) have small sigma; anchors on the flat middle have large sigma. The fit then trusts long slices that end low much more than short ones. (This replaces the brief's fixed "above 80% or below 15%" rule.)

### 14.7 Combining all slices into one score

Weighted total least squares, as in Plett 2011. Each slice says "E watt-hours moved the pack x points", and both E and x have errors. The capacity C (Wh at 100 points) that best explains all slices minimizes

  J(C) = sum over slices of (E_i - C x_i / 100)^2 / (sigma_E,i^2 + (C / 100)^2 sigma_x,i^2)

- Solved by a golden-section search for C between 20% and 160% of 26.6 Wh (a one-number search; at most 32 slices, so it costs nothing on the ESP32).
- Uncertainty from the curvature of J at the best C, sigma_C^2 = 2 / J''(C). If the slices disagree more than their sigmas allow (J per degree of freedom above 1), sigma_C is scaled up by that amount. Then a floor of 3 points (PLACEHOLDER, for the generic table's shape) is added in quadrature.
- Outliers: a slice whose normalized residual is over 3 is dropped from the fit and logged as an outlier, then the fit runs again once.
- Memory: the last 32 accepted slices. Older ones drop out, so the score follows the pack as it ages.
- SOH = C / 26.6 x 100, sigma_SOH = sigma_C / 26.6 x 100.
- Confidence: **none** (no accepted slice yet), **low** (sigma above 6 points), **high** (sigma 6 points or less).
- Display (Cole's choice): the number shows as soon as there is one accepted slice, always with its plus-or-minus. With confidence none, the pill shows learning progress instead (14.14).

### 14.8 Expected accuracy

From this table and the uncertainties above, for a pack at 94% SOH (one slice, then three similar slices):

| Use since a full charge | About | One slice | Three slices |
|---|---|---|---|
| Down to 91% | 2 hours | plus or minus 24 | plus or minus 14 |
| Down to 85% | 3 hours | 14 | 8 |
| Down to 70% | 6 hours | 8 | 5 |
| Down to 30% | 15 hours | 6 | 4 |
| Down to 10% | 19 hours | 4 | 3.5 |

These are the random parts only. If the real pack's curve differs from the generic table more than 15 mV per cell, the true error is larger; the disagreement test in 14.7 widens the plus-or-minus when that shows up.

### 14.9 Cycles

Equivalent full cycles = cumulative discharged Wh / 26.6, saved to flash. Energy drawn while the firmware thinks the charger is connected is not counted, because the charger supplies the load then. Charger detection is inferred, so this can overcount if a charge is missed.

### 14.10 Persistence (Preferences namespace "station")

Each blob starts with a version byte and ends with a CRC32, so an old or corrupt value resets cleanly instead of loading garbage. Keys are at most 15 characters.

| Key | Holds | Written |
|---|---|---|
| `remWh` (exists) | energy left for the existing estimate | unchanged |
| `sohState` | fit result (C, sigma), cycle Wh, the last slice result and whether it reached Firebase | when a slice closes; otherwise at most every 5 min, only if cycle Wh moved by 0.05 Wh or more |
| `sohSlices` | the last 32 accepted slices (E, x, sigma_E, sigma_x, about 600 bytes) | when a slice closes |
| `sohOpen` | the open slice: start anchor, Wh and Ah so far, temperature sum, gap seconds, lowest V_oc, best end anchor | at most every 5 min during a stretch; removed when it closes |

Flash wear: at most about 300 small writes a day, the same rate the firmware already uses for `remWh`.

### 14.11 Serial commands

| Key | Action |
|---|---|
| `h` | Print SOH debug: fit result, every stored slice, the open slice, and which anchor checks fail right now. |
| `z` | Reset all SOH state. Asks for `y` within 10 s; anything else cancels. |

### 14.12 Data contract (O9: separate nodes, approved)

`/station/latest` and `/station/history` do not change.

`/station/health`, overwritten at most once a minute when something changed, and right after a slice closes (about 230 bytes):

| Key | Type | Meaning |
|---|---|---|
| `ts` | ms epoch | Server time of this write |
| `battSohPct` | number or null | SOH from the fit, raw (can be above 100). Null while confidence is none. |
| `battSohSigmaPct` | number or null | Its plus-or-minus, in points |
| `battSohConf` | "none", "low", "high" | Confidence (14.7) |
| `battCapWhEst` | Wh or null | Fitted usable energy |
| `battCycles` | number | Equivalent full cycles |
| `battSohSlices` | count | Slices in the fit |
| `battSohProgressPct` | 0 to 100 | Progress of the open slice toward being usable; 100 once usable |
| `battSohLearn` | "waiting", "discharging", "charging" | What the learner sees now |
| `inaCal` | bool | False while the INA219 gain and offset are placeholders |

`/station/healthLog/<pushId>`, one record per closed slice (about 190 bytes): `ts`, `ok` (bool), `why` (reason when rejected or an outlier), `start` ("full" or "rest"), `e` (Wh), `ah`, `x` (SOE points), `sx` (its sigma), `sliceSohPct` (this slice alone: E / x scaled to SOH), `fitSohPct` and `fitSigmaPct` (the fit after this slice; null after a `z` reset), `tempC` (mean), `hours`.

Read rules (section 5, approved with O9): `health` public, `healthLog` only through a `ts`-ordered query of at most 500 records.

### 14.13 Budget

| Item | Today | With battery health |
|---|---|---|
| History growth | 3.8 MB/day, 1 GB in about 262 days | plus about 0.01 MB/day (a few log records) |
| One open tab, per day | 18 MB | plus at most 0.33 MB (usually about 0.04) |
| 24 h history load | 3.8 MB | same; the health log fetch is under 100 KB |

Measured: a health node is 227 bytes, a health log record about 190 bytes with its key. For comparison, the six fields in every record (the brief's layout) would have added 121 bytes per record: 4.9 MB/day of history and 23 MB/day per open tab.

### 14.14 Website

**Pill** next to the battery percent, a button that opens the detail panel:

| State | Pill shows |
|---|---|
| Confidence high or low | "SOH 94% ±6" with a small "est." |
| Confidence none | "SOH learning 42%" (progress of the first slice) |
| No health data (node missing or null) | "SOH --" |
| Station offline | grey, like the other cards |

Colors (O12, approved), never by color alone (the band name is in the panel and in the screen-reader label):

- 80% and above: good. Cited: the USABC manual puts end of life at 80% of rated capacity.
- 60% to 80%: fair. Still works, with less runtime.
- Under 60%: replace. Not cited: no free, checkable source was found (IEC 61960-3 reportedly uses 60% in its endurance test, but it is paywalled and secondary sources disagree). This line is a project choice, and the spec and report say so.

**Panel** (tap or click the pill; hover also opens it on a desktop; Escape or a click outside closes it): estimated usable energy with its plus-or-minus against 26.6 Wh rated, equivalent cycles, confidence in words, slices used, what the learner sees now, last update (from the newest log record), the last slice's result and reason, "uncalibrated" while `inaCal` is false, and one line: "Estimate from voltage and current, not a lab capacity test." No resistance (Cole's choice).

**Battery health chart:** a new "Battery health" tab in the graphs section, always all-time (the 15 min to 24 h buttons do not apply there and say so). The fitted SOH as a stepped line with its plus-or-minus as a shaded band; each slice's own value as a point (filled circle for slices from a full charge, hollow triangle for slices from a rest point, with a legend); a dashed 80% line labeled "end-of-life line (USABC)". The line breaks after a `z` reset. Rejected slices are listed under the chart with their reasons.

**Data:** a listener on `/station/health` (tiny, changes rarely), and the health log fetched once (`orderByChild("ts")`, `limitToLast(500)`) and again when a slice closes. Config: `soh: { ratedWh: 26.6, goodAtOrAbovePct: 80, replaceBelowPct: 60 }`.

### 14.15 Tests

- **Firmware logic on the PC:** `soh.h` is plain C++ with no Arduino headers. `tests/firmware/soh_test.cpp` compiles it with the system compiler (`npm run test:firmware`, also in CI). Synthetic discharge curves in `tests/fixtures/soh/`, generated by a script from a simulated pack with a known true capacity and labeled SYNTHETIC in every file. Cases:
  - a full discharge recovers the true SOH within its plus-or-minus;
  - short slices give a wide plus-or-minus that shrinks as slices are added;
  - rejections: too cold, too hot, no temperature, too small, unstable current (no anchors), gaps;
  - charger plugged in mid-stretch closes the slice at the last good anchor;
  - reboot mid-stretch closes the slice at the saved anchor;
  - an outlier slice is dropped; the fit follows when the true capacity drops (aging);
  - persistence round trip, and a corrupt blob resets.
- **Firmware build:** `arduino-cli compile` stays clean (no new warnings).
- **Website unit tests:** band and pill text for every state.
- **Playwright:** pill in each state (none, low, high, null, offline); panel opens and closes (click, Escape); health chart renders with a gap; no console errors. Fixtures labeled as before.

### 14.16 Known accuracy limits

- The generic voltage table can be off by tens of millivolts per cell for this pack. That is the biggest error source, especially for short slices.
- Slices that end in the flat middle of the curve (about 40 to 60%) are weak; the uncertainty reflects it.
- Until the INA219 is calibrated, every energy number carries the shunt tolerance (about 1%) and the INA219 gain error.
- SOH here is energy based (what the station cares about). Papers usually report capacity (Ah); the log keeps both. Energy SOH falls a little faster than capacity SOH because rising resistance also lowers the voltage.
- Temperature is the box air, not the cells.
- Charging is inferred. A missed charge would usually show up as a voltage rise and close the slice, but it can overcount cycles.
- A 3S pack is limited by its weakest cell; the estimate is for the pack as a whole, which is what matters to the station.
- Discharging below about 9.5 V is never needed, and the firmware never asks for it.

### 14.17 References (each checked on 2026-10-31)

1. G. L. Plett, "Recursive approximate weighted total least squares estimation of battery cell total capacity," *Journal of Power Sources*, vol. 196, no. 4, pp. 2319-2331, 2011. doi:10.1016/j.jpowsour.2010.09.048.
   Used for the core method: capacity from accumulated charge versus change in SOC over partial segments, where both carry errors, weighted by uncertainty, so no full discharge is needed. Checked: Crossref record; the method as described in the matching patent (US 8,041,522) and in MathWorks' implementation. This project solves the weighted total least squares cost directly by a one-number search over a small stored set of slices, instead of Plett's recursive approximation.
2. K. S. Ng, C.-S. Moo, Y.-P. Chen, Y.-C. Hsieh, "Enhanced coulomb counting method for estimating state-of-charge and state-of-health of lithium-ion batteries," *Applied Energy*, vol. 86, no. 9, pp. 1506-1511, 2009. doi:10.1016/j.apenergy.2008.11.021.
   Used for the SOH definition: health from the capacity the pack actually releases, measured by counting what it delivers. Checked: Crossref record, and the abstract on RePEc ("the state-of-health (SOH) is evaluated by the maximum releasable capacity").
3. United States Advanced Battery Consortium, *Electric Vehicle Battery Test Procedures Manual*, Revision 2, January 1996. Glossary, Appendix F: end of life is when the net delivered capacity is less than 80% of rated capacity, or peak power is less than 80% of rated.
   Used for the 80% line. Checked: read in the PDF published on uscar.org. Caveat: an electric-vehicle convention; one recent paper says later revisions dropped the fixed percentage (not confirmed).

Not cited: IEC 61960-3 (paywalled, could not be checked).

### 14.18 Tests only Cole can run (on the real station)

1. INA219 two-point calibration against a meter in series (sets the gain and offset; every SOH number depends on it).
2. Measure the charger's full voltage at the jack. Full anchors assume about 12.6 V; below 12.54 V they never trigger.
3. Normal use for a few charge cycles: check that `healthLog` gets one record per discharge and the plus-or-minus shrinks.
4. Plug the charger in partway through a discharge: the slice should close at its last good anchor.
5. Power-cycle with the button during a discharge: the slice should close at its saved anchor and a new one start.
6. Confirm the BMP388 reads sensible air temperature.

### 14.19 Plan (after approval)

One local commit per step, after Cole approves this section.

| Step | Work | Acceptance |
|---|---|---|
| S1 | `soh.h` pure logic, synthetic curve generator, C++ tests | All firmware tests pass on the PC |
| S2 | Firmware: INA219 averaging, SOH wiring, persistence, `h` and `z`, health uploads, version 1.1 | Compiles with no new warnings; secret scan passes |
| S3 | Website: data source, pill and panel, health chart tab, fixtures | Unit and Playwright tests pass at both sizes, no console errors |
| S4 | Docs: README, firmware README, this section marked built; final report | Report lists what was verified, what was not, and the accuracy limits |
