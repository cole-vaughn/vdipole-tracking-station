# V-Dipole Tracking Station

A live dashboard for a student-built 137 MHz ground radio station (ELEE 4230/6230). An ESP32 reads a broadband RF power
detector, environment sensors, a touch pad, a fan tach and the battery, and writes them to Firebase Realtime Database.
This static site shows them live, with a 3D view of the radio link between a test transmitter (a NanoVNA playing a
radio tower) and the station, placed at the station's own distance estimate (`SPEC.md` section 16). The earlier
satellite-tracking version of the site (3D globe, Meteor-M2-4 passes) is kept on the git branch `satellite-tracking`.
The station also works out a reception quality score (RQS) from its radio reading every 2 s: a 0 to 100 score that
combines a noise baseline and detection (OS-CFAR) with the station's health, and in ground mode a distance estimate
from signal strength (log-distance path loss, calibrated on site). It is the course "go beyond" algorithm, `SPEC.md`
sections 14 and 16, and it is the large instrument on the page. Satellite mode (a match against predicted passes) is
still in the firmware.

`SPEC.md` is the source of truth for what the site does and why.

## How it fits together

```
ESP32 --(HTTPS, signed in)--> Firebase Realtime Database --(public read)--> this site (GitHub Pages)
GitHub Actions, every 6 h --> CelesTrak orbit data --> site/data/gp.json --> this site
                                                   \-> site/data/passes.json (next 48 h of passes) --> the ESP32
```

- The site needs no key or password. It only reads, and the database address is public by design.
- Nothing on the page is made up. Missing readings show `--`. Test data is only used by the tests and the local
  fixture preview, and it is labeled on screen.

## Folders

| Path | What it is |
|---|---|
| `site/` | The website. This folder, and only this folder, is published. |
| `site/js/config.js` | Every setting: database address, station location, thresholds, RF and charging constants. |
| `site/js/lib/` | Pure logic with unit tests (freshness, battery, charging inference, reception quality score, SGP4, graph data). |
| `site/data/gp.json` | Orbit data. Refreshed on every deploy. |
| `site/data/passes.json` | The pass schedule the station fetches for its score. Made on every deploy by `tools/make-passes.mjs`; never committed (it expires after 48 h). |
| `site/vendor/` | Self-hosted three.js (with orbit controls, for the 3D link view) and satellite.js (for the pass schedule tool), bundled by `npm run vendor` (do not edit by hand). |
| `tests/` | Unit tests, browser tests, firmware logic tests (`tests/firmware/`, with SYNTHETIC radio readings and passes), the labeled fixture data source, the local preview server, the secret scan. |
| `tools/fetch-orbit.mjs` | Downloads orbit data from CelesTrak. |
| `tools/make-passes.mjs` | Writes the station's pass schedule from the orbit data. |
| `tools/vendor.mjs` | Rebuilds `site/vendor/` from the versions pinned in `package.json`. |
| `firmware/` | ESP32 firmware, two calibration sketches (touch pad, thermistor) and its own README (pin map, Firebase setup, hardware checklist). |
| `docs/` | `rqs-validation.md`: the tests that check the score on the real station. Real results go in `docs/validation/`. |

## Run it on your computer

Needs Node.js 22 or newer.

```sh
npm install
npx playwright install chromium   # only needed for the browser tests
npm run dev:fixtures              # http://localhost:4173 with labeled TEST FIXTURE data
npm run dev                       # http://localhost:4173 with the real database (once config.js has its address)
```

The site has no build step. The 3D and orbit libraries are bundled once into `site/vendor/` and committed. After
changing the version of `three` or `satellite.js` in `package.json`, run `npm install` and `npm run vendor`, then
commit the result.

## Tests

```sh
npm test               # firmware logic tests, unit tests, then browser tests (desktop and 360 px phone)
npm run test:firmware  # reception quality score logic (rqs.h) on this computer (add -- -v for details)
npm run screenshots    # writes tests/screenshots/desktop.png and phone.png for review
npm run scan           # secret scan (also runs before every commit and in CI)
```

The pre-commit hook lives in `.githooks/`. Turn it on once per clone: `git config core.hooksPath .githooks`.

## Connect the real database

1. Create the Firebase project, database, rules and the ESP32 user: see `firmware/README.md`, section "Firebase setup".
2. Put the database address in `site/js/config.js`, for example
   `databaseURL: "https://your-project-default-rtdb.firebaseio.com"`. This address is public; it is not a secret.
3. Push to `main`. The workflow tests and deploys.

Never put the ESP32 password, the WiFi password or the Web API key in this repo. They live only in
`firmware/station_firmware/secrets.h`, which git ignores, and the secret scan blocks it.

## Deploy (GitHub Pages)

One-time setup: repo Settings, Pages, Source: "GitHub Actions". After that every push to `main` runs
`.github/workflows/site.yml`: secret scan, unit tests, browser tests, then deploy of `site/`.

The same workflow runs every 6 hours to refresh orbit data. GitHub pauses scheduled workflows in a repo with no
activity for 60 days. If the page says the orbit data is stale, open the Actions tab, pick "Test and deploy",
enable it if needed, and press "Run workflow".

## Add a satellite

1. Add an entry to `satellites` in `site/js/config.js` (NORAD number, name, downlink note).
2. Run `node tools/fetch-orbit.mjs` and commit `site/data/gp.json`.

v1 shows the first satellite in the list. The orbit code works for any of them.

## Keep the database inside the free plan

- History grows about 6 MB a day. The 1 GB free storage lasts roughly 5 to 6 months.
- Before it fills: Firebase console, Realtime Database, Data. Select `station/history`, export it with the menu
  if you want a copy, then delete the `history` node. The live readings are not affected; graphs restart empty.
- The site never downloads more than 24 hours of history, closes its live connection while the tab is hidden, and
  the database rules refuse any history read that is not a limited, time-ordered query.

## Honest limits

- The RF reading is one broadband power number, not a spectrum. A rise can be interference. The reception quality
  score checks a rise against the predicted pass, which is evidence, not proof. Its weights and thresholds are guesses
  until real passes are logged (`docs/rqs-validation.md`), and its temperature is box air at the BMP388, not the RF
  detector chip.
- The battery percent is an estimate. "Charging" is only ever inferred, and its badge stays off until it is tuned on a
  real logged charge (`charging.showBadge` in `config.js`; the live inference is visible in Diagnostics).
- The thermistor carries an unverified legacy calibration and is tagged "unverified" until checked against the BMP388.

## Credits

- Earth textures: NASA Blue Marble and NASA Earth at Night (public domain), in the versions shipped with three-globe.
- Libraries: three.js, globe.gl and three-globe (MIT), satellite.js (MIT), Chart.js (MIT), Firebase JS SDK (Apache 2.0).
- Orbit data: CelesTrak. The background stars are drawn in code.
