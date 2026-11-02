# V-Dipole Tracking Station: Dashboard v2 Spec

Status (2026-11-01, evening): the site and firmware 1.3 now focus on a ground radio station (section 16): a test transmitter (the NanoVNA "tower"), a 3D view of the link, ground mode for the score, and a distance estimate from signal strength. The satellite pages are kept on the git branch `satellite-tracking`; the satellite design below (sections 7 to 8, 14) stays as the record of that work and of firmware satellite mode.
Earlier status: v1 site built (section 13). Battery state of health was cancelled on 2026-11-01 (Cole) and is replaced by the reception quality score (section 14) as the course "go beyond" algorithm. Section 14 and the firmware 1.2 changes in section 15 were built on 2026-11-01 (not committed until Cole approves): tested on a PC and against labeled fixtures, compiled for the ESP32, not yet run on the station. Build report: 14.22.
Last updated: 2026-11-01. This file replaces the draft PDF as the source of truth.

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
| D21 | Cancelled 2026-11-01. Battery state of health (SOH) was built (commits 6ff2797 to 7213c52, never run on the station) and is removed from the firmware, the site, the tests and the data contract. Git history keeps the record. Kept from that work: the INA219 128-sample averaging (cleaner current for the battery estimate) and the PC test runner for firmware logic. See 14.21. | Cole replaced it with the reception quality score. | Cole, 2026-11-01 |
| D22 | The course "go beyond" algorithm is the reception quality score (RQS): one 0 to 100 score from the RF reading, combining a causal OS-CFAR baseline and detection (Rohling 1983), a pass match against the free-space path loss shape (Friis 1946) and a health factor. Temperature for every RQS need comes from the BMP388 only, never the thermistors. Design in section 14. | Course requirement; ties the RF chain to the satellite. | RQS brief and Cole's answers, 2026-11-01 |
| D23 | LCD (Cole, 2026-11-01): five pages. At power on it shows RF strength and the RQS; each touch goes to the next page: temperatures (thermistor and BMP388), pressure and altitude, fan RPM, battery current and time left, then back to RF. Nothing else on the LCD. See 15.1. | Cole's request. | Cole, 2026-11-01 |
| D25 | Ground-station focus (Cole, 2026-11-01): after the real pass showed no measurable satellite signal (`docs/validation/2026-11-01-real-pass.md`), the site and the demo focus on a ground radio receiver station with a test transmitter: the NanoVNA at 137.5 MHz plays a radio tower. The page is renamed "V-Dipole Tracking Station". The globe, satellite readouts and pass table move to the git branch `satellite-tracking`; main keeps the pass schedule tools so firmware satellite mode still works. | Cole's request for the demo. | Cole, 2026-11-01 |
| D26 | Distance estimate (Cole, 2026-11-01): the station estimates its distance to the tower from signal strength with a log-distance path-loss model calibrated on site (serial `d`), sent with a likely range. Firmware, not the site, does it (Cole's choice), so it is logged to the cloud. Ground mode (firmware 1.3, the default) sets the satellite pass part aside. See section 16. | Cole's choice; adds a fifth measurement to the go-beyond algorithm. | Cole, 2026-11-01 |
| D24 | Calibration sketches for the capacitive sensor and the thermistor, separate from the station sketch. Each walks through a measurement over Serial and prints `#define` lines to paste into `station_firmware.ino`. See 15.2. | Cole's request. | Cole, 2026-11-01 |

## 3. The station and its data

Hardware status on 2026-10-31 (from Cole): almost done. The fan tach is wired and working. What is left is RF: power the SAWbird LNA from a stripped USB-C cable, then check the RF chain with a NanoVNA (O15). The station firmware compiles; the full first-run checklist in the firmware README has not been done yet.

So the site must handle null, missing and unverified data from day one.

| Subsystem | What the number can and cannot mean |
|---|---|
| BMP388 | Air temperature and pressure. Altitude uses a fixed 1013.25 hPa sea level, so it drifts with weather and is not true altitude. The board was physically damaged, so treat it as unconfirmed until Cole checks it. `tempC` is the raw `bmp.temperature` reading (checked 2026-11-01: no offset or constant is added; the only processing is the chip's own factory trim and its noise filter setting). It sits elsewhere in the box, away from both the AD8307 and the ESP32 (Cole, 2026-11-01), so it reads box air. It is the only temperature the RQS uses (section 14). |
| Thermistor | Carries a +96.2 C legacy offset in the conversion (the constant was named `BMP388_CALIBRATION_OFFSET` but is added to the thermistor, not the BMP388; renamed `THERM_OFFSET_C` in 1.2). Show an "unverified" tag on every thermistor value and chart until Cole confirms it agrees with the BMP388. One config flag removes the tag. Not used by the RQS. The calibration sketch in 15.2 replaces the offset with fitted constants. |
| RF chain | One broadband power number in dBm (about -75 dBm with no signal). Not a spectrum. Cannot tell a satellite from interference by itself; the RQS pass match (14.8) is how the station argues that a rise is the satellite. Flat until the SAWbird has power (planned: a stripped USB-C cable) and the chain is checked with a NanoVNA (O15). |
| Touch pad | touched, count since boot, delay, delta, fault flag. Thresholds need re-tuning after final assembly. |
| INA219 | Voltage, current and power on the 12 V line, between the button and the buck converter. It measures what the system draws. |
| Battery estimate | An estimate. Derating 0.90 and internal resistance 0.25 ohm are placeholders. Always shown with "~". |
| Fan | RPM from the tach on GPIO18 (10k series resistor, 10k pull-up to 3.3 V on the fan side; see the firmware README), 2 pulses per turn. Wired and working (2026-10-31): about 1,050 to 1,110 RPM on the 12 V battery, about 650 RPM on USB power from a laptop (used only while flashing). RPM falls as the supply voltage falls. |
| ESP32 | WiFi RSSI and uptime. |

## 4. Repo, naming and hosting

- Names (approved): repo and folder `vdipole-tracking-station`; sketch `firmware/station_firmware/station_firmware.ino` (the Arduino IDE needs the folder name to match the sketch name). The local working folder is renamed by Cole at handoff, because renaming it mid-session breaks the agent session.
- The secret scan also fails on the old project name, so it cannot creep back in.
- The draft PDF is not committed. This file replaces it.
- Proposed layout:

```
SPEC.md  README.md  .gitignore
site/            the published website (index.html, js/, css/, data/gp.json, data/passes.json)
tests/           unit tests, Playwright tests, tests/fixtures, tests/firmware (never published)
firmware/        README.md, station_firmware/ (sketch, rqs.h, secrets.example.h),
                 cal_capacitive/ and cal_thermistor/ (calibration sketches, 15.2)
docs/            rqs-validation.md (procedures for Cole); docs/validation/ holds real results only after Cole runs them
.github/workflows/   tests, secret scan, orbit data and pass schedule job, Pages deploy
(.claude/skills/ holds local helper skills; git-ignored, not published)
```

- Hosting (approved): GitHub Pages on a public repo, deployed from `site/` by a GitHub Actions workflow, so tests, fixtures and firmware are not published.
- Secret scan: a check that fails on a staged `secrets.h` or on strings that look like keys or passwords. It must skip `.claude/skills/` because the Playwright docs there contain sample passwords such as `pass123`.

## 5. Architecture and free-plan budget

```
ESP32 --sign in, PUT latest 2 s, POST history 10 s--> Firebase RTDB --stream + history query--> Browser (static site)
GitHub Action (every 6 h) --> CelesTrak GP data --> site/data/gp.json --> Browser (same origin)
                          \-> SGP4 for the next 48 h --> site/data/passes.json --HTTPS GET every 6 h--> ESP32 (RQS pass match, 14.8)
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
| Record size | 417 bytes of JSON, about 441 stored with its push key, before the RQS | Measured 2026-10-31 by building the firmware's JSON with realistic values (13-digit `ts`, every field at its sent precision). Re-check on the first real record. |
| With the RQS (14.13) | history record about 637 bytes (about 661 stored); `/station/latest` about 697 | Measured 2026-11-01 from the firmware's own RQS JSON (`rqs::appendJson`, in the PC tests): the 12 fields add 220 bytes, the 4 parts in `latest` 60 more. Re-check on the first real record. |
| History growth | about 5.7 MB/day (was 3.8) | 1 GB lasts about 175 days (was 262). |
| One always-open tab | about 30 MB/day (was 18) | About 0.9 GB/month. 50% of the download limit is about 5 such tabs (was 9). |
| 24 h history load | about 5.7 MB (was 3.8) | Fine now and then, not on every page view. |
| Pass schedule | about 1.6 to 1.8 KB per fetch, 4 fetches a day | From GitHub Pages, not Firebase. No Firebase cost. |

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

Replace the placeholder email only in the Firebase console. Turn off new sign-ups if the console allows it. The `health` and `healthLog` entries were removed with battery health (D21, Cole's request 2026-11-01). Firebase is not set up yet, so no live rules change. The RQS needs no new node: its fields live in `latest` and `history`, which the rules above already cover.

## 6. Fields

The 25 original keys under `/station/latest` (and each history record): ts, tempC, pressHPa, altM, thermRaw, thermFilt, rfRawDbm, rfFiltDbm, capTdUs, capDeltaUs, capPF, touched, touchCount, capFault, battV, battmA, battmW, battSocPct, battWhLeft, battHoursLeft, battPeakmA, fanRpm, fanStalled, rssi, uptimeS. The RQS adds 12 keys to both (rqs, rqsLabel, rqsTag, rfExcessDb, rfBaselineDbm, rfThresholdDbm, rfSigmaDb, rfDetected, passElDeg, passCorr, nextPassEpoch, rqsTempC) and 4 to `latest` only (rqsSnr, rqsDet, rqsPass, rqsHealth); see 14.12. Also `/station/touch` {state, count, ts} and `/station/meta` {fw, bootTs, rqsAlphaT}.

| Where | Fields |
|---|---|
| Status bar | ts (age), uptimeS, rssi |
| RQS instrument (the hero, 14.14) | rqs, rqsLabel, rqsTag, rqsSnr, rqsDet, rqsPass, rqsHealth, passElDeg, passCorr, nextPassEpoch, rqsTempC; rqs from history for the sparkline |
| Battery widget | battSocPct, battHoursLeft, battV, battmA, battmW, battWhLeft, battPeakmA (detail) |
| RF meter | rfFiltDbm, rfExcessDb (level), rfBaselineDbm; rfRawDbm, rfThresholdDbm, rfDetected (graph) |
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

**F4 RF visuals.** Driven by dB above a rolling baseline, not absolute dBm. (Changed 2026-11-01:) the baseline now comes from the station (`rfExcessDb`, `rfBaselineDbm`, computed by the RQS in 14.5), and the browser's own 30 minute baseline is removed, so the page and the score never disagree. Level = clamp(rfExcessDb / 12 dB, 0 to 1); level 0 while `rfExcessDb` is null. Pulsing rings from the marker (none at level 0), a beam line only while the satellite is above the horizon, and an S-meter that prints real dBm and dB above baseline plus satellite elevation. Labeled "Signal strength (broadband)" with a note that a rise can be interference. No spectrum or waterfall. Idle is normal and is not faked.

**F5 Touch.** Card with TOUCHED or idle, counter since boot, last touch time, "SENSOR FAULT" on capFault. Reacts through `/station/touch`. Ripple on the marker plus a brief card highlight (D10). Without the globe loaded, the card highlight alone.

**F6 Fan.** SVG blades, visual speed capped near 3 turns per second, real RPM always printed. States: spinning, stalled (fanStalled or under 200 RPM), not connected (null). Caption notes that speed falls with battery voltage.

**F7 Graphs.** Lower section, tabs or collapsible groups. Keep the four v1 charts (BMP388 temperature/pressure/altitude on separate axes, thermistor raw vs filtered, RF raw vs filtered, BMP388 vs thermistor). Add battery, fan, capacitive delta, RSSI. Ranges 15 min, 1 h, 6 h, 24 h. Gaps for null. Chart.js, pinned. (2026-11-01:) the RF chart gains the baseline and threshold lines and shaded pass windows and detections, and a new RQS history chart and detection log join it (14.14).

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
- Firmware TLS currently skips certificate checks on the two Firebase connections. Pin the Google root before treating the uplink as secure. Firmware task, Cole decides when.
- The pass schedule fetch (14.8) does check the certificate, with the ESP32 core's built-in certificate bundle (`useBuiltinCACertBundle()`, in core 3.3.12). A forged schedule could only change the score, never the uplink.

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
| O6 | Bench check: thermistor vs BMP388, BMP388 health. (Fan tach done 2026-10-31: wired to GPIO18 and reading correctly.) | Cole |
| O7 | Firmware changes the agent wants to ask for (none made yet): rename user-visible old-name strings (splash text, version string, flash namespace), charge-aware battery estimate, slimmer history records, TLS pinning. | Cole decides |
| O8 | Take down v1 after v2 is live. Consider exporting ThingSpeak data first. | Cole |
| O9 to O14 | Battery health items. Cancelled with battery health on 2026-11-01 (D21). | Closed |
| O15 | RF: power the SAWbird from a stripped USB-C cable, then confirm the RF chain with a NanoVNA. The last hardware task. | Cole |
| O16 | Decided 2026-11-01: the BMP388 sits elsewhere in the box, away from the AD8307 and the ESP32. It reads box air (14.4). | Done |
| O17 | Run the RQS validation procedures in `docs/rqs-validation.md`. (c) real pass done 2026-11-01: no detection, any signal below about 0.13 dB, floor about 18 dB above thermal (`docs/validation/2026-11-01-real-pass.md`). Still to do: (a) NanoVNA CW test (the demo, `docs/demo-script.md`), (b) warm-up test, and the 50 ohm load test of the floor. | Cole |
| O18 | After Pages is enabled: put the pass schedule address (`https://<user>.github.io/vdipole-tracking-station/data/passes.json`) in `secrets.h` as `PASSES_URL`. Until then the station tags every score NO SCHEDULE. | Cole |
| O19 | Decided 2026-11-01 (Cole left it to the agent): `/station/meta` carries `rqsAlphaT`, and all 12 RQS fields stay in history. | Done |
| O20 | Run the two calibration sketches (15.2) and paste their `#define` lines into the station sketch. | Cole |

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

A11. **Two TLS clients on an ESP32 can exhaust heap.** The firmware keeps two secure clients alive. This is a firmware risk, not a site risk, noted for the compile-and-run step. (2026-11-01:) the pass schedule fetch adds a third client object, but it connects only every 6 hours and closes right after, while the sign-in client is closed, so at most two TLS sessions are open at once, as before.

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

## 14. Reception quality score (the course "go beyond" algorithm)

Status: designed 2026-11-01 from the RQS brief and Cole's answers, and built the same day (R0 to R5, 14.20; build report 14.22). Not committed until Cole approves. Not yet run on the station. Every constant in 14.11 is a PLACEHOLDER until measured, except the few marked "fixed" (physics or statistics). Items added by the agent on top of the brief are marked "(added)"; changes found while building are marked "(built)".

### 14.1 What it is, and how it meets the course

The RQS answers one question every 2 seconds: "how good is what the antenna hears right now, and is it the satellite?" It is a number from 0 to 100, a label (NO SIGNAL, WEAK, GOOD, STRONG) and a tag that says what it was compared against (PASS, NO PASS, NO SCHEDULE, TEST).

| Course requirement | How this design meets it |
|---|---|
| Ties to published science | The baseline and detection are a causal ordered-statistic CFAR detector (Rohling 1983). The expected power during a pass follows the Friis free-space equation (Friis 1946): received power falls with the square of the distance. The temperature behaviour comes from the AD8307 datasheet. The robust spread uses the scaled median absolute deviation (Rousseeuw and Croux 1993). See 14.18. |
| Combines measurements into one score | RF power (AD8307), BMP388 temperature, battery charge (INA219 estimate), the clock (NTP) and the satellite's predicted path (SGP4 from CelesTrak data) become four parts and one weighted score (14.10). |
| Logged to the cloud | The score and every part that explains it go into `/station/latest` every 2 s and `/station/history` every 10 s (14.12). |

### 14.2 What the station can and cannot measure

- **One broadband number.** The AD8307 reports total power in the SAW filter's band. It cannot see a spectrum, so it cannot prove a rise is Meteor-M2-4. The pass match (14.8) is the evidence: a rise that follows the predicted distance curve during a predicted pass is very likely the satellite; a rise outside any pass is tagged NO PASS (possible interference) and can score at most 70.
- **The reading is already smoothed.** `rfFiltDbm` comes from the firmware's Kalman filter (Q 0.01, R 1.0), which settles to a gain of about 0.1 per sample: roughly a 20 s time constant. Its spread is small, which is why sigma has a floor (14.5), and a detection lags a real rise by some seconds.
- **Temperature is box air.** The BMP388 sits elsewhere in the box, away from the AD8307 and the ESP32 (Cole, 2026-11-01). It measures the air at its own board, not the AD8307 die. The AD8307 runs a little warmer than the air around it (it draws about 8 mA at 5 V by the datasheet, about 40 mW). A box that warms evenly is fine; a fan draft or sun on one side is not. This is said in "Why this score?" on the site.
- **The antenna pattern is not modelled.** The expected power uses distance only. The V-dipole's gain changes with elevation and direction, so a real pass will not follow the curve exactly. The correlation (14.8) measures shape, not level, which helps, but a pattern null can still lower it.
- **No new hardware.** Everything uses sensors that already exist.

### 14.3 Inputs

| Input | From | Every |
|---|---|---|
| `rfFiltDbm` | AD8307 chain, Kalman filtered (existing) | 2 s |
| `rfRawDbm` | AD8307 chain, raw (existing; sent for the graph, not used in the score) | 2 s |
| `tempC` | BMP388 `bmp.temperature`, raw (existing; NaN when `bmpOk` is false or a read fails) | 2 s |
| `battSocPct` | battery estimate (existing, kept from before) | 2 s |
| UTC time | NTP, new: `configTime(0, 0, "pool.ntp.org", "time.nist.gov")`. Counted as set once `time()` is past 2026-01-01. | on demand |
| Pass schedule | `data/passes.json` from GitHub Pages, new (14.8) | fetched every 6 h |

`rqs.h` is plain C++ with no Arduino headers (like `soh.h` was), so the whole algorithm is tested on a PC. The sketch only gathers inputs, calls `rqs::Engine::step()` once per 2 s sample, and sends the result.

### 14.4 Step 1: temperature (BMP388 only)

The thermistors are never used here: not for compensation, not for the health factor, not as a fallback.

1. **Sanity check** of each raw BMP388 sample. It is rejected if `bmpOk` is false, the read failed (NaN), it is outside -20 to 85 C, or it jumped more than 5 C from the previous in-range sample (2 s earlier).
2. **Smoothing:** a first-order filter with a 60 s time constant (alpha = 1 - exp(-dt / 60 s)), so sensor noise cannot leak into the score. It starts from the first good sample. (added) If no good sample arrives for 60 s, the filter starts over from the next good one instead of reusing an old value.
3. **A rejected sample** sets temperature compensation to 0 and H_temp to 1 for that sample, and the site shows "TEMP N/A". Nothing is made up and no old value is reused for that sample. `rqsTempC` is null then.
4. **Compensation:** rf_comp = rfFiltDbm - ALPHA_T x (T - T_REF), with T the smoothed BMP388 temperature and T_REF 25 C (the datasheet's specification temperature).

ALPHA_T defaults to 0 (no compensation) until the warm-up test fits it. What the AD8307 datasheet says (Rev. D, read 2026-11-01):

- It promises "highly stable scaling over temperature" but gives no per-degree coefficient.
- Slope: 23 to 27 mV/dB at 25 C, and 23 to 27 mV/dB over the full -40 to +85 C range. The temperature limits are the same as the part-to-part limits.
- Intercept: -87 to -77 dBm at 25 C, and -88 to -76 dBm over the full range. The temperature limits are only 1 dB wider at each end.
- So the AD8307 itself should drift well under 1 dB over a normal day in the box. The warm-up test measures the whole chain anyway (AD8307, LM324 gain stage, mux, ESP32 ADC), because any of them can drift. If the fitted ALPHA_T is smaller than its own uncertainty, it stays 0.

**Warm-up test (procedure in `docs/rqs-validation.md`, for Cole):** antenna replaced by a 50 ohm terminator, so the input power is constant. Start the station cold (after a night unpowered, or after an hour in a cooler place), lid on, and log `rfFiltDbm` and the smoothed BMP388 temperature for 2 hours while it warms. Fit rfFiltDbm = a + ALPHA_T x (T - 25) by least squares, with the fit's standard error. Set `RQS_ALPHA_T_DB_PER_C` to the slope only if it is clearly not zero. The firmware prints a CSV line for this (serial `w`, 14.15).

### 14.5 Step 2: causal OS-CFAR baseline

- **Ring buffer:** the last 15 minutes of rf_comp, 450 samples at 2 s.
- **Guard window:** the newest 30 s (15 samples) are left out, so the rising edge of a signal does not lift its own baseline.
- **Training cells:** every older sample in the ring (up to 435).
- **baseline** = the 20th percentile of the training cells (an ordered statistic, as in OS-CFAR).
- **sigma** = 1.4826 x MAD of the training cells (MAD = median absolute deviation from their median; 1.4826 makes it match the standard deviation for normal noise), with a floor SIGMA_MIN = 0.3 dB, because the Kalman filter makes the raw spread very small.
- **Freeze:** while a detection is active, new samples are not added to the ring, so a pass cannot raise its own baseline. Adding resumes 60 s after the detection clears.
- (added) **Freeze limit:** adding resumes after 15 minutes of freeze even if the detection is still on. A pass above 5 degrees lasts at most about 13 minutes at 820 km (computed: 23 degrees of Earth central angle each side, 101 minute orbit), so this never cuts a real pass short. Without it, a lasting step in the noise floor (the SAWbird powered on, a new interferer) would keep "detected" on forever.
- (added) **Warm-up:** no score until the ring holds at least 150 training cells (5 minutes after a start). During warm-up `rqs`, `rfBaselineDbm`, `rfThresholdDbm`, `rfSigmaDb` and `rfExcessDb` are null, `rfDetected` is false, and `rqsLabel` is "WARMING UP".
- Nothing is saved to flash. After a restart the baseline is learned again in 5 minutes (no flash wear).
- Why OS-CFAR: Rohling's ordered-statistic CFAR sets the threshold from a ranked reference sample instead of a mean, so one strong sample in the reference cells cannot drag the threshold up. Classic CFAR works on linear power and multiplies; the AD8307 output is already in dB, where that multiplication becomes an addition, so the threshold is baseline + K x sigma.

### 14.6 Step 3: detection with hysteresis

- threshold = baseline + K x sigma, with K = 4.
- **Detected** after N = 3 samples in a row above the threshold (6 s).
- **Cleared** after 3 samples in a row below baseline + (K - 1) x sigma (the lower line is the hysteresis, so a signal sitting at the threshold does not flicker).

### 14.7 Step 4: sub-scores (each clamped to 0 to 1)

- excess = rf_comp - baseline (dB).
- S_snr = excess / D_FULL_DB, with D_FULL_DB = 12 dB.
- S_det = 1 while detected, else 0.

### 14.8 Step 5: pass match

**Schedule file.** The deploy workflow (already runs every 6 h for the orbit data) also writes `site/data/passes.json` for the next 48 hours, using the same SGP4 code and the same rounded station coordinates as the website (33.92 N, 84.84 W), minimum elevation 5 degrees:

```json
{
  "source": "SGP4 (satellite.js) from CelesTrak GP data",
  "generated": 1793500000, "validUntil": 1793672800, "noradId": 59051,
  "tleEpoch": "2026-10-30T22:38:57Z", "minEl": 5, "step": 30,
  "passes": [ { "aos": 1793512345, "los": 1793513012, "maxEl": 42.7, "el": [5.0, 8.1, 11.6, 15.2] } ]
}
```

- Times are Unix seconds, UTC. `el` holds the elevation every 30 s from `aos`, ending at `los`.
- About 8 to 12 passes in 48 hours, about 3 KB.
- (added) A pass in progress when the file is made starts at the file's start time, so it is not lost.
- If CelesTrak fails, the workflow keeps the committed orbit data (existing rule) and the schedule is made from it; the station checks `validUntil`.

**On the ESP32.**

- Fetched over HTTPS (`WiFiClientSecure` with the core's built-in certificate bundle) every 6 hours by the upload task. (added) After a failed fetch it retries every 15 minutes.
- Parsed by a small hand-written parser in `rqs.h` (no JSON library). Kept in RAM only: up to 24 passes of up to 40 elevations each (about 4 KB).
- The address is `PASSES_URL` in `secrets.h` (O18). It is not secret, but it is per-install. It stays empty until Pages is enabled.
- A schedule is valid only if the clock is set (NTP), it parsed cleanly, and now is before its `validUntil`.

**Expected power during a pass (Friis).** In free space, received power falls as 1 / r^2, so relative to the satellite straight overhead:

  E(t) = -20 x log10( r(el) / r_zenith )

  r(el) = sqrt( (Re + h)^2 - (Re x cos(el))^2 ) - Re x sin(el), with Re = 6371 km, h = 820 km, r_zenith = h

- el(t) comes from the schedule, linearly interpolated between the 30 s points.
- E is 0 dB overhead and -10.75 dB at 5 degrees elevation, where the range is 2,825 km (computed from the formula). A low pass that peaks at 15 degrees spans only -10.75 to -8.05 dB.

**Correlation.**

- Inside a pass window (aos <= now <= los), each sample adds the pair (excess, E). The pairs are cleared when a new pass starts.
- Pearson r is computed over the pass so far, kept as running sums, so it costs nothing per sample.
- It needs at least 10 pairs (20 s). Before that, and when either value has not varied at all, r is null (added: S_shape is then 0).
- S_shape = clamp((r - 0.3) / 0.6).
- S_pass:

| State | S_pass | Tag |
|---|---|---|
| Inside a pass window | 0.4 + 0.6 x S_shape | PASS |
| Valid schedule, outside every pass | 0 | NO PASS |
| No valid schedule (no file, no clock, expired, parse error) | 0.5 | NO SCHEDULE |
| Test mode (serial `t`, for the NanoVNA demo) | 0.5 | TEST |

- Test mode is never saved (added), so a restart always ends it.
- Pearson r measures shape, not level, so a strong but flat signal during a pass does not get full marks. A signal that rises and falls with the predicted curve does.

### 14.9 Step 6: health factor

H = H_batt x H_temp.

- **H_batt:** 1 at a charge of 20% or more, falling in a straight line to 0.5 at 0%. (added) If the charge is unknown (INA219 missing), H_batt is 1 and the site shows "BATT N/A", the same rule as the temperature.
- **H_temp:** 1 up to 45 C, falling in a straight line to 0.7 at 60 C, and (added) staying at 0.7 above 60 C. It uses the smoothed BMP388 temperature (box air). It is 1 when the BMP388 is unavailable or its sample was rejected (14.4).

### 14.10 Step 7: score, label and tag

  RQS = 100 x H x (0.50 x S_snr + 0.20 x S_det + 0.30 x S_pass x S_det), clamped to 0 to 100, sent with one decimal.

(built) **The pass part counts only while something is detected** (the "x S_det" above). Found by the PC tests: with the brief's formula, plain noise with no schedule scored 15.8 (WEAK), because NO SCHEDULE alone gives 0.30 x 0.5 = 15 points, and noise during a pass could reach WEAK too, because random noise correlates a little with the curve. With the change, noise stays NO SIGNAL in every case (worst 4.5 in the tests), and everything the brief intended still holds: a strong signal outside a pass scores at most 70, with no schedule at most 85, and a matching pass up to 100. `rqsPass` is still sent as the pass part itself, so the bar shows how well the pass matches; the "Why this score?" panel says it only counts while a signal is detected.

| RQS | Label |
|---|---|
| under 15 | NO SIGNAL |
| 15 to under 40 | WEAK |
| 40 to under 70 | GOOD |
| 70 and above | STRONG |
| no score yet (warm-up) | WARMING UP (added) |

- Outside a pass, S_pass is 0, so even the strongest signal scores at most 70 and is tagged NO PASS. That is intentional: a strong signal when no satellite is overhead is more likely interference. The site explains this.
- With no schedule, the most is 85. In a pass with a signal that follows the curve, 100.

### 14.11 Constants (top of `rqs.h`)

| Constant | Value | Status |
|---|---|---|
| `RQS_RING_N` | 450 (15 min at 2 s) | PLACEHOLDER |
| `RQS_GUARD_N` | 15 (30 s) | PLACEHOLDER |
| `RQS_MIN_TRAIN_N` | 150 (5 min) | PLACEHOLDER (added) |
| `RQS_BASE_PCT` | 20 | PLACEHOLDER |
| `RQS_MAD_SCALE` | 1.4826 | fixed (normal-noise consistency) |
| `RQS_SIGMA_MIN_DB` | 0.3 | PLACEHOLDER |
| `RQS_K` | 4 | PLACEHOLDER |
| `RQS_N_ON`, `RQS_N_OFF` | 3, 3 | PLACEHOLDER |
| `RQS_UNFREEZE_S` | 60 | PLACEHOLDER |
| `RQS_MAX_FREEZE_S` | 900 | PLACEHOLDER (added) |
| `RQS_D_FULL_DB` | 12 | PLACEHOLDER |
| `RQS_W_SNR`, `RQS_W_DET`, `RQS_W_PASS` | 0.50, 0.20, 0.30 | PLACEHOLDER |
| `RQS_PASS_BASE`, `RQS_CORR_LO`, `RQS_CORR_SPAN`, `RQS_CORR_MIN_N` | 0.4, 0.3, 0.6, 10 | PLACEHOLDER |
| `RQS_PASS_UNKNOWN` | 0.5 (NO SCHEDULE and TEST) | PLACEHOLDER |
| `RQS_RE_KM`, `RQS_ORBIT_H_KM` | 6371, 820 | fixed (mean Earth radius; Meteor-M2-4 orbit height) |
| `RQS_ALPHA_T_DB_PER_C` | 0 | PLACEHOLDER until the warm-up test |
| `RQS_T_REF_C` | 25 | fixed (datasheet specification temperature) |
| `RQS_TEMP_TAU_S`, `RQS_TEMP_MIN_C`, `RQS_TEMP_MAX_C`, `RQS_TEMP_JUMP_C` | 60, -20, 85, 5 | PLACEHOLDER |
| `RQS_BATT_FULL_PCT`, `RQS_BATT_H_EMPTY` | 20, 0.5 | PLACEHOLDER |
| `RQS_HOT_START_C`, `RQS_HOT_END_C`, `RQS_H_HOT` | 45, 60, 0.7 | PLACEHOLDER |
| `RQS_LABEL_WEAK`, `RQS_LABEL_GOOD`, `RQS_LABEL_STRONG` | 15, 40, 70 | PLACEHOLDER |
| `RQS_SCHED_EVERY_S`, `RQS_SCHED_RETRY_S` | 21600, 900 | PLACEHOLDER |
| `RQS_MAX_PASSES`, `RQS_MAX_EL` | 24, 40 | fixed (memory sizes) |

### 14.12 Data contract (needs Cole's OK: it adds fields)

Added to `/station/latest` and every `/station/history` record:

| Key | Type | Meaning |
|---|---|---|
| `rqs` | 0 to 100 or null | The score, one decimal. Null during warm-up. |
| `rqsLabel` | text | NO SIGNAL, WEAK, GOOD, STRONG, or WARMING UP |
| `rqsTag` | text | PASS, NO PASS, NO SCHEDULE, or TEST |
| `rfExcessDb` | dB or null | rf_comp minus baseline |
| `rfBaselineDbm` | dBm or null | The OS-CFAR baseline |
| `rfThresholdDbm` | dBm or null | baseline + K x sigma |
| `rfSigmaDb` | dB or null | The robust spread, after its floor |
| `rfDetected` | bool | Detection state (14.6) |
| `passElDeg` | degrees or null | Expected elevation now, inside a pass window only |
| `passCorr` | -1 to 1 or null | Pearson r over the pass so far |
| `nextPassEpoch` | Unix seconds or null | AOS of the next pass that has not started yet |
| `rqsTempC` | C or null | The smoothed BMP388 value the score used; null when the sample was rejected |

Added to `/station/latest` only (Cole, 2026-11-01), for the four bars on the site: `rqsSnr`, `rqsDet`, `rqsPass`, `rqsHealth` (each 0 to 1, two decimals; null during warm-up).

Removed: `/station/health` and `/station/healthLog` with their read rules (D21).

`/station/meta` also carries `rqsAlphaT`, the compensation in use in dB per C (O19, decided): the site shows "compensation off (not measured yet)" while it is 0.

Measured (2026-11-01, PC tests): the 12 fields add 220 bytes to a record, the 4 parts 60 more to `latest`.

### 14.13 Budget

| Item | Before | With the RQS |
|---|---|---|
| History record | 417 bytes (441 stored) | about 637 (661 stored) |
| `/station/latest` | 417 bytes | about 697 |
| History growth | 3.8 MB/day, 1 GB in about 262 days | about 5.7 MB/day, 1 GB in about 175 days |
| One open tab | 18 MB/day | about 30 MB/day (0.9 GB/month), so about 5 tabs fill 50% of the download limit |
| 24 h history load | 3.8 MB | about 5.7 MB |

(built) Measured 2026-11-01 from the firmware's own RQS JSON in the PC tests, added to the 417 byte record measured on 2026-10-31. Kept: all 12 fields in history (O19). If the budget gets tight, `rqsLabel` and `rfThresholdDbm` could leave history (the site can work them out), saving about 50 bytes per record.

### 14.14 Website

**RQS instrument, the hero (top of the page, beside the globe on wide screens, first on phones):**

- A large gauge from 0 to 100 with four colour bands that match the labels, the number in the middle, and the label and tag beside it in words (never colour alone).
- Four small bars: Strength (rqsSnr), Detection (rqsDet), Pass match (rqsPass), Health (rqsHealth), each with its value.
- A 30 minute sparkline of `rqs`, from the history the page already loads plus live points. (built) The history query always covers at least 30 minutes, so the 15 minute range loads 30 minutes (about 120 KB) instead of a second query.
- Next pass time (from `nextPassEpoch`), and the expected elevation during a pass.
- "Why this score?", a panel in plain words: what each part means and its value now; the NO PASS rule ("a strong signal outside a pass window can score at most 70, because it is more likely interference"); the temperature note ("box air at the BMP388, not the AD8307; compensation on or off"; "TEMP N/A" when the sample was rejected); "BATT N/A" when the charge is unknown; WARMING UP explained; NO SCHEDULE explained (no clock, no file yet, or expired); TEST explained.
- Null or offline: greyed like the other cards, last values readable. Records without RQS fields (older firmware) show "--".

**Kept:** the S-meter, the rings and the beam, now driven by `rfExcessDb` (F4).

**RF graph:** the raw and filtered lines, plus the baseline and threshold lines, shaded pass windows (where `passElDeg` is not null) and shaded detections (where `rfDetected` is true).

**RQS history chart:** `rqs` over the chosen range, with faint lines at 15, 40 and 70.

**Detection log**, under the radio graphs, built from the loaded history (Cole, 2026-11-01): start time, duration, peak excess, peak RQS, and whether it was in a pass. It covers the chosen graph range. A detection shorter than the 10 s history step can be missed; the log says so.

**Data:** no new listener or node. Everything rides on the existing `latest` stream and history queries. Fixtures carry made-up RQS values for every state and stay labeled TEST FIXTURE DATA. (built) The fixture's made-up pass schedule is not the real orbit, so in the fixture preview the globe can show the satellite below the horizon while the score says PASS.

### 14.15 Serial commands and LCD

| Key | Action |
|---|---|
| `t` | Toggle TEST MODE (S_pass 0.5, tag TEST). Not saved. For the NanoVNA demo. |
| `q` | (added) Print the RQS state: baseline, sigma, threshold, freeze state, every part, the schedule (valid or why not, next 3 passes), the clock. |
| `w` | (added) Toggle a CSV line every 2 s for the warm-up test: `WARM,ms,rfFilt,rfRaw,tempRaw,tempSmooth,rfComp`. |
| `h`, `z` | Removed with battery health. |

The LCD gets a full redesign from Cole (15.1); the RQS is on its first page.

### 14.16 Tests

- **Firmware logic on the PC:** `tests/firmware/rqs_test.cpp`, compiled with warnings as errors (`npm run test:firmware`, in CI). Every input is SYNTHETIC and labeled. Cases:
  - percentile and MAD with the guard window left out;
  - the sigma floor;
  - freeze while detected, unfreeze 60 s after clearing, and the 15 minute freeze limit;
  - N in a row to detect, and the hysteresis to clear;
  - warm-up;
  - the Friis range function at 90, 5 and 0 degrees;
  - Pearson r (perfect, opposite, flat, fewer than 10 pairs);
  - the composite score at known parts, the label edges, and the tag logic (in pass, out of pass, no schedule, expired, no clock, test mode);
  - BMP388 unavailable (compensation off, H_temp 1, `rqsTempC` null), out of range, and a jump;
  - the 60 s smoothing;
  - the schedule parser on good, cut-short and junk input;
  - a full synthetic pass that is detected, matches the curve, and scores STRONG, PASS;
  - the same rise outside a pass, which scores at most 70, NO PASS;
  - the JSON for the new fields, and its measured size.
- **Pass schedule generator:** unit tests that `tools/make-passes.mjs` agrees with the site's own pass finder and keeps the file small.
- **Firmware build:** `arduino-cli compile` with all warnings, none from the sketch or `rqs.h`.
- **Website unit tests:** RQS view logic (label, tag text, bars, Why panel lines, detection log from history).
- **Playwright, desktop and phone:** the hero in every state (WARMING UP, NO SIGNAL, WEAK, GOOD, STRONG; PASS, NO PASS, NO SCHEDULE, TEST; TEMP N/A; BATT N/A; null; offline; older record without RQS fields); the Why panel opens and closes; the sparkline; the RF overlays; the RQS chart; the detection log; no console errors; no sideways scroll at 360 px.

### 14.17 Known limits

- Broadband only: the score cannot prove the source. The pass tag and correlation are evidence, not proof.
- The BMP388 reads box air at its own spot, not the AD8307 die (14.2).
- ALPHA_T is 0 until the warm-up test; until then there is no compensation at all.
- The antenna pattern is not modelled, so a real pass will not follow the distance curve exactly.
- Every weight and threshold is a guess until real passes are logged (O17).
- Kalman lag: detection starts some seconds after a real rise.
- After a restart, there is no score for 5 minutes (warm-up).
- No schedule until Pages is live and `PASSES_URL` is set (O18). Until then every score is tagged NO SCHEDULE and capped at 85.
- The schedule is only as good as the orbit data; the workflow refreshes it every 6 hours, but GitHub can pause schedules on a quiet repo (A8). The station checks `validUntil`.

### 14.18 References (each checked on 2026-11-01)

1. H. Rohling, "Radar CFAR Thresholding in Clutter and Multiple Target Situations," *IEEE Transactions on Aerospace and Electronic Systems*, vol. AES-19, no. 4, pp. 608-621, July 1983. doi:10.1109/TAES.1983.309350.
   Used for the ordered-statistic CFAR: a ranked reference sample sets the threshold, so a few strong samples cannot raise it. Checked: Crossref record (title, author, volume, issue, pages, date). This project adapts it to a log detector and one time series (14.5).
2. H. T. Friis, "A Note on a Simple Transmission Formula," *Proceedings of the IRE*, vol. 34, no. 5, pp. 254-256, May 1946. doi:10.1109/JRPROC.1946.234568.
   Used for the expected power curve: in free space, received power falls with the square of the distance (14.8). Checked: Crossref record.
3. Analog Devices, *AD8307: Low Cost, DC to 500 MHz, 92 dB Logarithmic Amplifier*, data sheet. Rev. D (2008) was read; analog.com lists Rev. F as current.
   Used for the slope (25 mV/dB) and intercept (-84 dBm) already in the firmware, and their limits over temperature (14.4). Checked: the Rev. D PDF (via an Octopart mirror, because analog.com timed out), specification table: slope 23 to 27 mV/dB at 25 C and over temperature; intercept -87 to -77 dBm at 25 C and -88 to -76 dBm over temperature; operating range -40 to +85 C. A search listing of analog.com's own PDF names it Rev. F, but the download timed out, so Rev. F was not read. Re-read these values from Rev. F before the final course report.
4. P. J. Rousseeuw and C. Croux, "Alternatives to the Median Absolute Deviation," *Journal of the American Statistical Association*, vol. 88, no. 424, pp. 1273-1283, 1993. doi:10.1080/01621459.1993.10476408.
   Used for the 1.4826 factor that scales the MAD to match a standard deviation for normal noise. Checked: Crossref record.

### 14.19 Validation (Cole runs these; `docs/rqs-validation.md`)

The procedure document has empty tables and no numbers. Real results go in `docs/validation/` only after Cole runs them.

- **(a) NanoVNA CW test.** NanoVNA as a signal source, start = stop = 137.5 MHz. The open cable end is held at 5 m, 3 m and 1 m from the antenna. For each: wait for the score to settle, put the station in TEST MODE (`t`), and record `rfRawDbm`, `rfExcessDb` and `rqs` in a table. Expected: higher excess and score as the cable gets closer. This is not a calibration: the NanoVNA output level and the near-field coupling are not known well.
- **(b) Warm-up test for ALPHA_T** (14.4), with the 50 ohm terminator and the `w` CSV.
- **(c) A real pass log:** pick a pass over 30 degrees from the site's table, leave the antenna up, log the history across it, and fill in the detection start, peak excess, `passCorr` and peak RQS.

### 14.20 Plan (after Cole approves)

No commits until Cole approves (his rule for this task).

| Step | Work | Acceptance |
|---|---|---|
| R0 | Remove battery health from the firmware, site, tests and docs (14.21); keep the INA219 averaging and the PC test runner | Firmware compiles with no warnings; all remaining tests pass. Done. |
| R1 | `rqs.h` and its PC tests | All cases in 14.16 pass with clang and GCC. Done: 142 checks. |
| R2 | `tools/make-passes.mjs`, the workflow step, firmware 1.2 (NTP, schedule fetch, RQS fields, `t`, `q`, `w`, the new LCD pages, the renamed thermistor offset) | Compiles with no warnings; secret scan passes. Done. |
| R3 | Website: hero, Why panel, overlays, RQS chart, detection log, fixtures, tests | Unit and Playwright tests pass at both sizes, no console errors. Done. |
| R4 | Calibration sketches (15.2) | Both compile with no warnings. Done. |
| R5 | Docs: validation procedures, both READMEs, build report | Report lists what was verified and what only Cole can verify. Done: 14.22. |

### 14.21 Removed: battery state of health (D21)

- Firmware: `soh.h`, the health section of the sketch, the `/station/health` and `/station/healthLog` uploads, the `sohState`, `sohSlices` and `sohOpen` flash keys, the `h` and `z` commands, and the Health line in the Serial print. (No board ever ran 1.1, so no board holds those keys.)
- The voltage table moves back into the sketch. `BATT_R_INT_OHM` gets its 0.25 ohm placeholder back directly.
- Site: the health pill, panel and chart tab, `lib/health.js`, `ui/health.js`, the health listeners, the `soh` config, and the health fixtures.
- Tests: `soh_test.cpp`, `pack_sim.h`, `health.test.mjs`, and the four health browser tests.
- Docs: the health rules, nodes, keys and placeholders in both READMEs. The old design stays readable in git history (commits 6ff2797 to 7213c52).
- Fields the brief named (`battSohPct`, `battSohConf`, `battCapWhEst`, `battRintMohm`, `battCycles`, `battSohProgressPct`): none are in `latest` or `history`. The ones that existed lived only in `/station/health`, which goes away. `battRintMohm` was never built.
- Kept: the battery estimator (SOC, hours left), the INA219 128-sample averaging, and the firmware test runner (`tests/firmware/run.mjs`).

### 14.22 Build report (2026-11-01)

**Verified (on a PC, against labeled fixtures, and by compiling):**

- Firmware 1.1 compiled before any change (the brief thought it did not): 1,112,112 bytes of flash, no warnings from the sketch.
- `rqs.h`: 142 checks with SYNTHETIC inputs (`npm run test:firmware`), every case in 14.16. Built with clang (warnings as errors) and checked with the ESP32's GCC. Highlights: a synthetic pass 20 dB above the noise, following the free-space curve, is detected, correlates at r 0.93 and peaks at 100 (STRONG, PASS); the same rise outside a pass peaks at exactly 70 (NO PASS), with no schedule at 85; six hours of noise give no false detection and never leave NO SIGNAL; a lasting +10 dB step unfreezes at 15 minutes and clears after 27.
- The pass schedule: `tools/make-passes.mjs` agrees with the website's own pass finder to the second (3 unit tests), and the C++ parser in `rqs.h` reads the file it writes (part of the firmware tests). From the committed orbit data: 9 to 11 passes in 48 hours, about 1.6 KB, the longest 12.8 minutes.
- Firmware 1.2 compiles for the ESP32 with all warnings on and none from the sketch or `rqs.h` (the only warnings are inside the Adafruit INA219 library, as before): 1,110,588 bytes of flash (84%), 60,920 bytes of RAM (18%). Both calibration sketches compile the same way: 287,236 and 324,936 bytes.
- The website: 29 unit tests and 28 browser tests (desktop and 360 px phone): every hero state (WARMING UP, the four labels, the four tags, TEMP N/A, BATT N/A, null, offline, a record from older firmware), the Why panel, the sparkline, the RF overlays and shading, the score chart, the detection log, no console errors, no sideways scroll.
- The calibration sketches' copied code matches the station sketch (5 unit tests).
- The references in 14.18: Rohling 1983, Friis 1946 and Rousseeuw and Croux 1993 on Crossref; the AD8307 datasheet Rev. D read (Rev. F not downloadable).
- Secret scan passes; no em dashes; `secrets.h` (now holding Cole's home WiFi) stays git-ignored.

**Changed while building** (marked "(built)" above): the pass part counts only while something is detected (14.10); the RQS JSON size is measured, not estimated (14.13); the history query covers at least 30 minutes for the sparkline; `site/data/passes.json` is made on every deploy and never committed, and no schedule is made from orbit data older than 7 days.

**Only Cole can verify (needs the station, O15 to O20):**

- Anything in 1.2 running on the ESP32: NTP, the schedule fetch over HTTPS, the new LCD pages, `t`, `q`, `w`, the RQS fields reaching Firebase.
- Whether the RF chain responds at all (SAWbird supply, NanoVNA test, `docs/rqs-validation.md` test a).
- ALPHA_T (warm-up test b) and a real pass (test c). Until then every weight and threshold is a guess.
- The two calibration sketches on the real touch pad and thermistor.
- The live Firebase path: there is still no Firebase project (O1).

## 15. Firmware 1.2: LCD pages and calibration sketches (Cole, 2026-11-01)

Built 2026-11-01. Compiled, not run on the station.

### 15.1 LCD pages

At power on (after the short start-up messages), the LCD shows page 0. Each confirmed touch goes to the next page; after the last it returns to page 0. These five pages are the whole LCD; the old pages for raw RF, capacitive debug, battery voltage and power, and WiFi status go away. That information is still in the Serial print and on the website.

| Page | Line 1 | Line 2 |
|---|---|---|
| 0, RF and RQS | `RF -75.1 +12.3dB` (filtered dBm and excess; `RF -75.1 dBm` while warming up) | `RQS 57 GOOD`, then every 2 s `RQS 57 NO PASS` (the label and tag take turns; `NO SCHED` short for NO SCHEDULE) |
| 1, temperatures | `BMP388  23.4 C` | `Therm   24.1 C` (`--` when unavailable) |
| 2, pressure | `P 1013.2 hPa` | `Alt 312 m` |
| 3, fan | `Fan 1080 RPM` | `ok`, `STALLED` or `off` |
| 4, battery | `I 105.3 mA` | `87% left 21h34m` (`LOW` under 20% or under 9.5 V) |

### 15.2 Calibration sketches

Two separate sketches, so the station sketch stays as it is: `firmware/cal_capacitive/cal_capacitive.ino` and `firmware/cal_thermistor/cal_thermistor.ino`. The commands and steps are at the top of each sketch. Each needs only the Serial Monitor (no PC software), walks through its steps, and prints `#define` lines to paste into `station_firmware.ino`. They copy the circuit constants and measurement code from the station sketch (an Arduino sketch cannot include a file from another folder). A unit test (`tests/unit/calibration.test.mjs`) checks that the copied constants, the measurement functions and the thermistor formula lines match the station sketch, so they cannot drift apart.

**Capacitive sensor** (same pins and timing as the station):

1. Idle: 30 s hands off. Records the delay's mean and noise.
2. Touch: three 10 s holds when prompted. Records the delay change during each.
3. It sets the press and release levels between the idle noise and the weakest touch, with a margin each side. It warns if the gap is too small to be reliable.
   - Prints `CAP_PRESS_DELTA_US` and `CAP_RELEASE_DELTA_US`.
4. Optional: measure the GPIO high level and CAP_REF with a meter and type them in.
   - Prints `CAP_VS` and `CAP_VREF`.
5. Optional: disconnect the pad wire and clip known capacitors (for example 0, 10, 22, 47, 100 pF) where it connected, typing each value. A straight-line fit of delay against capacitance gives the real slope and offset.
   - Prints `CAP_PF_SLOPE_CAL`, `CAP_PF_OFFSET_CAL` and `CAP_TD_OFFSET_US` (0), with the fit's worst error. (built) `capPF` then reads the capacitance connected at the pad terminal, not counting the wiring's own.

**Thermistor** (same mux path as the station; the BMP388 or a reference thermometer as the reference):

1. Live view every second: ADC voltage, the thermistor resistance worked out from the circuit, the temperature without the offset, and a "steady" mark once the last 30 readings stop moving.
2. Capture a point with `p <reference C>` (for example in ice water, at room temperature, in warm water), or `p` alone to use the BMP388 when both sit in the same air. At least 2 points, 10 C or more apart; 3 is better.
3. `f` fits the beta model ln R = ln R0 + B x (1/T - 1/T0) by least squares. It prints `THERM_R0`, `THERM_BETA` and `THERM_OFFSET_C` (normally near 0 after the fit), with the error left at each point.
4. If the resistance comes out impossible (negative or far off the part's range), the circuit constants are wrong rather than the thermistor. It says so and lists which values to measure with a meter (`BRIDGE_VEXC`, `V_MID`, `V_BIAS`), instead of hiding it behind an offset. This is probably where the old +96.2 C offset came from.


## 16. Ground radio station: ground mode, distance estimate, 3D link view (2026-11-01)

Built 2026-11-01 for the CP5 demo (D25, D26). Firmware 1.3 compiles; not yet flashed. Site tested against labeled fixtures.

### 16.1 Ground mode (firmware `STATION_MODE_GROUND 1`, the default)

- The transmitter is on the ground (the NanoVNA "tower"), so the score's satellite pass part is set aside.
- RQS = 100 x H x (0.70 x S_snr + 0.30 x S_det), clamped 0 to 100. Same labels (15, 40, 70). Tag `GROUND`.
- `rqsPass`, `passElDeg`, `passCorr` and `nextPassEpoch` are null. The baseline, detection, health and temperature rules of section 14 are unchanged.
- Satellite mode (`STATION_MODE_GROUND 0`) is the section 14 design, unchanged; test mode `t` belongs to it.

### 16.2 Distance from signal strength

1. **Signal power:** the detector reads signal plus floor, so the floor (the baseline) is taken away as power: Ps = 10 log10(10^(P/10) - 10^(B/10)). Only when the reading stands at least 3 dB above the baseline.
2. **Model:** log-distance path loss, Ps(d) = P1 - 10 n log10(d / 1 m). In free space n = 2 (Friis); rooms and the ground usually give about 1.6 to 3.5.
3. **Calibration on site:** hold the tower at known distances and send `d 1`, `d 5`, `d 10` over USB. Each point averages the last 5 signal readings (10 s). Up to 6 points, saved in flash (key `distCal`). `d` shows the fit, `d x` clears it. With one point, n = 2 is assumed. With two or more, n and P1 are fitted by least squares on log10(d), and n is held inside 1 to 6.
4. **Estimate:** d = 10^((P1 - Ps) / (10 n)). Its likely range uses the fading spread sigma: d / f to d x f with f = 10^(sigma / (10 n)). sigma = 4 dB with fewer than 3 points, otherwise the fit's own scatter, at least 2 dB. All PLACEHOLDER (`rqs.h`).
5. **Limits:** the simple model holds beyond about 1 m (the wavelength is 2.18 m). Indoors, reflections move the reading several dB within half a meter; the orientation of the NanoVNA's cable end matters; the Kalman lag means 20 to 40 s to settle after a move. It only works for this known transmitter.
6. **Tests (SYNTHETIC):** a tower at -20 dBm at 1 m with n = 2.2 over the -75 dBm floor, calibrated at 1, 3 and 10 m: estimates 1.94 m at 2 m, 4.94 m at 5 m, 15.98 m at 15 m, each inside its range.

### 16.3 Data contract additions (Cole left the call to the agent, 2026-11-01)

| Key | Where | Meaning |
|---|---|---|
| `distM`, `distLoM`, `distHiM` | latest and history | Estimated distance and its likely range, meters; null without a calibration or a signal 3 dB clear |
| `distCal` | latest only | Calibration points (0 = not calibrated) |
| `distN` | latest only | Fitted path-loss exponent |
| `rqsTag` value `GROUND` | latest and history | Ground mode |
| `mode` | `/station/meta` | `ground` or `satellite` |

Measured (PC tests): history records about 680 bytes, latest about 765.

### 16.4 Website

- Renamed "V-Dipole Tracking Station". The globe, the satellite readouts and the pass table are on the branch `satellite-tracking`.
- **3D radio link** (three.js, self-hosted `site/vendor/scene.lib.js`): the station (box with its LCD showing page 0, touch pad, fan, mast, V-dipole at 120 degrees) at the center; a lattice tower with a beacon at the station's distance estimate; the likely range shaded on the ground; signal dots and ripples sized by the real level (none at 0). Ground distances to scale, models enlarged and labeled so. No estimate: the tower is faded and labeled "no estimate", never placed at a made-up distance. Drag to turn; reduced motion stops the animation.
- Score instrument: three bars in ground mode (the pass bar only in satellite mode), distance with its range, mode. "Why this score?" explains ground mode and the distance in plain words.
- Radio graphs: a distance chart (estimate, low, high); the detection log shows the median distance of each detection. Pass-window shading only appears in satellite mode.
- The RF card shows the distance in place of the satellite elevation.
