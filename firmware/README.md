# Station firmware 1.3

Single-sketch firmware for the Satellite V-Dipole Tracking Station (ESP32 DOIT DevKit V1, ELEE 4230/6230).
It reads every sensor, drives the LCD, and uploads to Firebase Realtime Database.

**Status (2026-11-01): version 1.2 ran on the station on 2026-11-01 (one real pass logged). Version 1.3 adds ground
mode and the distance estimate (`SPEC.md` section 16); it compiles cleanly and is not flashed yet.** The score logic in `rqs.h`
is tested on a PC (`npm run test:firmware`, 142 checks with SYNTHETIC inputs); the sketch around it has only been
compiled. Nothing here has been measured on the finished enclosure, so every value marked `PLACEHOLDER` in the sketch
or in `rqs.h` is a guess until it is measured. Walk through the test checklist at the bottom.

**New in 1.3:** ground mode (the default, `STATION_MODE_GROUND 1`): a ground transmitter (the NanoVNA "tower")
instead of satellite passes, so the score is strength and detection only, tagged GROUND. A distance estimate from signal
strength, calibrated on site with `d` (below) and saved in flash. New fields `distM`, `distLoM`, `distHiM` (latest and
history) and `distCal`, `distN` (latest only); `mode` in `/station/meta`. LCD page 0 shows the distance in turn with
the label.

**New in 1.2:**

- The reception quality score (RQS), the course "go beyond" algorithm: one 0 to 100 score from the RF reading
  (baseline, detection, pass match, health). Logic in `rqs.h`, tested on a PC. Design: `SPEC.md` section 14.
- NTP time and a pass schedule fetched from the website every 6 hours.
- Five LCD pages (RF and RQS first), per Cole (`SPEC.md` 15.1).
- Two calibration sketches, for the capacitive sensor and the thermistor (`SPEC.md` 15.2).
- Battery state of health (1.1) is removed. The INA219 averaging from 1.1 stays.

## Files

| File | Purpose |
|---|---|
| `station_firmware/station_firmware.ino` | The whole station firmware. Open this folder in the Arduino IDE (the folder name must match the sketch name). |
| `station_firmware/rqs.h` | Reception quality score logic as plain C++ with no Arduino headers, so the same file is tested on a PC (`tests/firmware/`). All its constants are at the top. |
| `station_firmware/secrets.example.h` | Template for credentials and the pass schedule address. Copy to `secrets.h` in the same folder and fill in. |
| `cal_capacitive/cal_capacitive.ino` | Calibration sketch for the touch pad: prints the `CAP_...` constants to paste into the station sketch. |
| `cal_thermistor/cal_thermistor.ino` | Calibration sketch for the thermistor: prints `THERM_R0`, `THERM_BETA`, `THERM_OFFSET_C`. |
| `.gitignore` | Keeps `secrets.h` and build output out of git. |
| `README.md` | This file. |

**Never commit `secrets.h`.** The older CP4 sketch (`CP4_full_sketch.ino`) contains a plaintext WiFi password and a
ThingSpeak write key. Do not copy anything from it into a repository. If it was ever pushed, rotate the WiFi password
and regenerate the ThingSpeak write key.

## Build setup

- Board: ESP32 Dev Module / "DOIT ESP32 DEVKIT V1" (arduino-esp32 core).
- Libraries (Library Manager): Adafruit BMP3XX, Adafruit INA219 (accept the BusIO and Unified Sensor dependencies).
- Built in: Wire, LiquidCrystal, WiFi, WiFiClientSecure, HTTPClient, Preferences.
- Serial Monitor: 115200 baud.
- Verified build (2026-11-01, version 1.2): arduino-esp32 core 3.3.12, Adafruit BMP3XX 2.1.6, Adafruit INA219 1.2.3,
  Adafruit BusIO 1.17.4, Adafruit Unified Sensor 1.1.15, LiquidCrystal 1.0.7. Board "DOIT ESP32 DEVKIT V1".
  Result: 1,110,588 bytes of flash (84%), 60,920 bytes of RAM (18%). With all warnings on, the sketch and `rqs.h` give
  none; the only warnings are inside the Adafruit INA219 library itself. The two calibration sketches compile the same
  way (about 290 and 325 KB).
- Command-line build (optional): `arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 firmware/station_firmware`
  (needs `secrets.h` in that folder). If it cannot find the Adafruit headers, add
  `--libraries ~/Documents/Arduino/libraries` (where the Arduino IDE installs them). If it says `LiquidCrystal.h` is
  missing, run `arduino-cli lib install "LiquidCrystal"`; the Arduino IDE already includes it.
- If the compiler complains about `portENTER_CRITICAL` or the `soc/gpio_reg.h` register names, the arduino-esp32 major
  version changed an API. Fix it there; the intent is "poll GPIO16 with interrupts off on this core while timing".

## Pin map

| Function | GPIO | Notes |
|---|---|---|
| I2C SDA / SCL | 21 / 22 | BMP388 at 0x77, INA219 at 0x40 (A0 and A1 to GND) |
| Mux ADC input | 34 | ADC1, input only. Thermistor and RF both arrive here, one at a time |
| BLOCK_T | 32 | HIGH blocks the thermistor channel |
| BLOCK_R | 12 | HIGH blocks the RF channel. **Strapping pin**: nothing may pull it HIGH at boot |
| Cap drive | 4 | Square wave out, through the 0.97 MOhm resistor |
| Cap comparator | 16 | LM339 output in, 10k pull-up to 3.3 V on the board |
| Fan tach | 18 | Tach wire (dashed on this fan) through a 10k series resistor; 10k pull-up to 3.3 V on the fan side (see below) |
| LCD RS, E, D4 to D7 | 27, 26, 25, 33, 14, 13 | 4-bit mode |

## Before connecting the fan tach wire (do this first)

A 3-pin PC fan has power (12 V), ground, and tach. The tach output is normally open collector: it pulls to ground and
otherwise floats. Some fans, or a board with its own pull-up, can put 12 V on that wire. 12 V on a GPIO destroys the ESP32.

1. Power the fan from 12 V with the tach wire **not** connected to anything.
2. Measure tach to ground with a multimeter. Expect about 0 V or a floating reading. If it reads anywhere near 12 V,
   stop: put a level shifter or a divider in between.
3. Only then wire it to GPIO18 like this:

   ```
   fan tach wire ---+--- 10k series ---> GPIO18
                    |
                   10k pull-up
                    |
                  3.3 V
   ```

   - The tach wire goes to a node. The node has a 10k pull-up to 3.3 V and a 10k series resistor to GPIO18.
   - The pull-up must be on the **fan side** of the series resistor, not at the GPIO. If it were at the GPIO, the two
     resistors would form a divider and a low would read about 1.6 V, which the ESP32 may not see as low.
   - The series resistor limits the current into the pin to under 1 mA if the tach wire ever went to 12 V.
   - The firmware sets the pin to plain `INPUT`. The external pull-up does the work. The ESP32's internal pull-up (about
     45k) would also sit on the GPIO side and lift a low to about 0.6 V, close to the 0.8 V limit for a low, so it is off.
4. Until the tach wire is connected, set `FAN_ENABLED` to `0` in the sketch. Otherwise the firmware reports 0 RPM and "stalled".

**Safety check done (Cole, 2026-10-31).** The tach (the dashed wire on this fan) was connected through a 10k resistor to
a GPIO set to `INPUT_PULLDOWN`. With the fan running it read HIGH 0 of 400 samples, and 400 of 400 when the resistor was
touched to 3.3 V. So the tach never drives the line high by itself: it is open collector and safe for 3.3 V logic.

## Behaviour summary

- **Every 2 s:** read BMP388, thermistor and RF through the mux (Kalman filtered), INA219, update the battery estimate,
  run one RQS step, compute fan RPM, refresh the shared snapshot.
- **Every 100 ms:** capacitive sensor. A confirmed press increments the counter and advances the LCD page. A press or
  release triggers an immediate write to `/station/touch`. The INA219 is also read every 100 ms.
- **Upload task (core 0, 16 KB stack):** signs in to Firebase Auth, then writes `/station/latest` every 2 s,
  `/station/history` every 10 s, `/station/touch` on a press or release, and `/station/meta` once per boot. Every 6 hours
  (15 minutes after a failure) it also fetches the pass schedule (`PASSES_URL`). The capacitive timing runs on core 1
  with interrupts off, so WiFi and TLS work cannot disturb it.
- **Clock:** NTP (`pool.ntp.org`, `time.nist.gov`), UTC. Until it is set, the RQS tags every score NO SCHEDULE.
- **Battery:** energy counting in watt-hours against 26.6 Wh x 0.90, a voltage-based correction used only near full and
  near empty, a smoothed power average for the time left, and the remaining energy saved to flash every 5 minutes.
- **INA219 averaging:** the chip averages 128 samples per reading, the sketch reads it every 100 ms and averages each
  2 s window, so random WiFi bursts no longer look like current noise. The library has no call for this, so the sketch
  writes the config register itself, and checks it every 2 s in case a sharp load reset the chip (`q` shows the count).
- **Reception quality score:** see the next section.
- **BMP388 missing:** not fatal. The sketch keeps running, the BMP388 fields are sent as `null`, and the RQS runs without
  temperature (compensation off, temperature factor 1).

## Reception quality score (RQS)

Full design and reasons: `SPEC.md` section 14. In short, once per 2 s sample:

1. **Temperature (BMP388 only, never the thermistors).** A raw sample is rejected if the BMP388 is missing, the read
   failed, it is outside -20 to 85 C, or it jumped more than 5 C since the previous sample. Good samples are smoothed with
   a 60 s filter. rf_comp = rfFiltDbm - ALPHA_T x (T - 25 C). ALPHA_T is 0 until the warm-up test.
2. **Baseline (causal OS-CFAR).** A 15 minute ring of rf_comp (450 samples). The newest 30 s are a guard window and are
   left out. baseline = 20th percentile of the rest; sigma = 1.4826 x MAD, at least 0.3 dB. The ring stops taking samples
   while a detection is on, and starts again 60 s after it clears (or after 15 minutes at most). No score for the first
   5 minutes after a start ("WARMING UP").
3. **Detection.** threshold = baseline + 4 x sigma. Detected after 3 samples in a row above it; cleared after 3 in a row
   below baseline + 3 x sigma.
4. **Parts.** S_snr = (rf_comp - baseline) / 12 dB; S_det = 1 while detected. Both 0 to 1.
5. **Pass match.** Inside a pass window from the schedule, the excess is compared with the free-space power curve
   E = -20 log10(r(el) / 820 km) by a Pearson correlation r (at least 10 samples). S_pass = 0.4 + 0.6 x clamp((r - 0.3) / 0.6)
   in a pass, 0 outside one, 0.5 with no valid schedule or in TEST MODE.
6. **Health.** H_batt: 1 at 20% charge or more, down to 0.5 at 0% (1 if the charge is unknown). H_temp: 1 up to 45 C, down
   to 0.7 at 60 C and above (1 when the BMP388 sample was rejected). H = H_batt x H_temp.
7. **Score.** RQS = 100 x H x (0.50 S_snr + 0.20 S_det + 0.30 S_pass x S_det), 0 to 100. The pass part counts only
   while something is detected, so plain noise never scores above NO SIGNAL (found in the PC tests, `SPEC.md` 14.10).
   Under 15 NO SIGNAL, 15 to 40 WEAK, 40 to 70 GOOD, 70 and above STRONG. Tag: PASS, NO PASS, NO SCHEDULE or TEST. A strong signal outside a pass scores at
   most 70 and is tagged NO PASS, on purpose: it is more likely interference.

Nothing of the RQS is saved to flash; after a restart it learns the baseline again in 5 minutes.

### Pass schedule

The website's deploy workflow writes `data/passes.json` every 6 hours: the next 48 hours of Meteor-M2-4 passes above
5 degrees for the rounded station location, from SGP4. The station fetches it over HTTPS with certificate checking (the
core's built-in certificate bundle) and keeps it in RAM. Set `PASSES_URL` in `secrets.h` once Pages is enabled, for
example `https://<github user>.github.io/vdipole-tracking-station/data/passes.json`. Empty means "no schedule": every score
is then tagged NO SCHEDULE.

### LCD pages (a capacitive press goes to the next page)

At power on the LCD shows page 0. After page 4 it returns to page 0.

| Page | Content |
|---|---|
| 0 | RF filtered dBm and dB above baseline; RQS with its label and tag taking turns every 2 s |
| 1 | Temperatures: BMP388 and thermistor |
| 2 | Pressure and altitude (BMP388) |
| 3 | Fan RPM and its state |
| 4 | Battery current, charge and time left |

### Serial commands

| Key | Action |
|---|---|
| `b` | Recapture the capacitive baseline (hands off the pad) |
| `s` | Toggle the capacitive CSV stream (`CAP,ms,td,delta,pF,pressed`) |
| `c` | Dump raw capacitive delays for one measurement set |
| `m` | Mux test: both blocked, thermistor only, RF only |
| `f` | Battery is full now (use right after a full charge) |
| `r` | Reset the peak current |
| `x` | Clear the saved battery value (reboot to restart from the voltage reading) |
| `t` | Toggle RQS TEST MODE (pass part fixed at 0.5, tag TEST; for the NanoVNA demo). Not saved: a restart ends it |
| `q` | RQS report: baseline, sigma, threshold, freeze state, each part, the schedule and the clock |
| `w` | Toggle the warm-up CSV (`WARM,ms,rfFilt,rfRaw,tempRaw,tempSmooth,rfComp`) for fitting ALPHA_T |
| `d 3` | Distance calibration: the transmitter is 3 m away right now (needs a signal 3 dB above the floor; averages the last 10 s). Up to 6 points, saved in flash |
| `d` | Show the distance calibration and the fit |
| `d x` | Forget the distance calibration |

## Firebase setup (Spark plan, no card needed)

1. Firebase console: create a project. Add a **Realtime Database** (start in locked mode, any region).
2. **Authentication**: enable the Email/Password provider. Create one user for the ESP32 with a long random password.
   If the console offers a setting to disable new sign-ups, turn it off, so nobody can create accounts with your Web API key.
3. **Rules** (replace the email with the ESP32 user, only in the console). The live nodes are public. History can
   only be read through a time-ordered, limited query, so nobody can download the whole node in a loop and burn the
   free download quota:

```json
{
  "rules": {
    "station": {
      "latest": { ".read": true },
      "touch":  { ".read": true },
      "meta":   { ".read": true },
      "history": {
        ".read": "query.orderByChild == 'ts' && query.limitToLast <= 9000",
        ".indexOn": ["ts"]
      },
      ".write": "auth != null && auth.token.email == 'esp32@example.com'"
    }
  }
}
```

4. Project settings, General: copy the **Web API key**. Realtime Database page: copy the database host
   (for example `your-project-default-rtdb.firebaseio.com`, without `https://`).
5. Fill in `secrets.h` from `secrets.example.h`.

The website reads `/station` directly with public read access, so no credentials ever ship to the browser. The Web API key
identifies the project and is not a secret by itself, which is why the write rule is locked to one email.

## Data contract

All keys live under `/station`. `null` means "sensor unavailable" or "not known yet"; NaN is never sent. `ts` is the
Firebase server time in milliseconds since the epoch (`{".sv":"timestamp"}`), so the website computes data age as
`Date.now() - latest.ts`.

| Key | Unit | Source |
|---|---|---|
| `ts` | ms epoch | Server timestamp |
| `tempC` | deg C | BMP388, raw `bmp.temperature` (no offset added) |
| `pressHPa` | hPa | BMP388 |
| `altM` | m | BMP388 (sea level 1013.25 hPa) |
| `thermRaw`, `thermFilt` | deg C | Thermistor chain raw and Kalman filtered |
| `rfRawDbm`, `rfFiltDbm` | dBm | AD8307 chain raw and Kalman filtered |
| `capTdUs` | us | Capacitive charge delay |
| `capDeltaUs` | us | Delay minus baseline |
| `capPF` | pF | Capacitance estimate (uncalibrated, see placeholders) |
| `touched` | bool | Current press state |
| `touchCount` | count | Presses since boot |
| `capFault` | bool | Comparator stuck or timed out |
| `battV` | V | Battery side voltage |
| `battmA`, `battmW` | mA, mW | Load current and power |
| `battSocPct` | % | Estimated charge |
| `battWhLeft` | Wh | Energy left |
| `battHoursLeft` | h | Smoothed time left (`null` when the draw is under 20 mW) |
| `battPeakmA` | mA | Peak current since boot or reset |
| `fanRpm`, `fanStalled` | RPM, bool | Tach (`null` RPM when `FAN_ENABLED` is 0) |
| `rssi` | dBm | WiFi signal |
| `uptimeS` | s | Seconds since boot |
| `rqs` | 0 to 100 or null | Reception quality score, one decimal. Null while warming up |
| `rqsLabel` | text | `NO SIGNAL`, `WEAK`, `GOOD`, `STRONG`, or `WARMING UP` |
| `rqsTag` | text | `GROUND` (ground mode), or in satellite mode `PASS`, `NO PASS`, `NO SCHEDULE`, `TEST` |
| `rfExcessDb` | dB or null | Compensated RF minus the baseline |
| `rfBaselineDbm` | dBm or null | OS-CFAR baseline (20th percentile of the training cells) |
| `rfThresholdDbm` | dBm or null | Baseline + K x sigma |
| `rfSigmaDb` | dB or null | 1.4826 x MAD, at least 0.3 |
| `rfDetected` | bool | Detection state |
| `passElDeg` | deg or null | Expected satellite elevation now, only inside a pass window |
| `passCorr` | -1 to 1 or null | Pearson r between excess and the expected power, over the pass so far |
| `nextPassEpoch` | Unix s or null | Start of the next pass that has not begun |
| `rqsTempC` | deg C or null | The smoothed BMP388 temperature the score used; null when the sample was rejected |
| `rqsSnr`, `rqsDet`, `rqsPass`, `rqsHealth` | 0 to 1 or null | The four parts (`rqsPass` null in ground mode). **In `latest` only**, not in history |
| `distM`, `distLoM`, `distHiM` | m or null | Ground mode: estimated distance to the transmitter and its likely range; null without a calibration or a signal 3 dB above the floor |
| `distCal`, `distN` | count, number | Ground mode: calibration points and the fitted path-loss exponent. **In `latest` only** |

Other paths:

- `/station/history/<pushId>`: the same fields as `latest` except the four RQS parts, one record every 10 s.
- `/station/touch`: `{ state, count, ts }`, written the moment the press state changes.
- `/station/meta`: `{ fw, bootTs, rqsAlphaT }`, once per boot. `rqsAlphaT` is the temperature compensation in use
  (dB per C, 0 until the warm-up test), so the website can say whether compensation is on.

The battery health nodes of 1.1 (`/station/health`, `/station/healthLog`) are gone.

### Flash (Preferences) keys

All in the namespace `station`. Keys are at most 15 characters (an NVS limit).

| Key | Type | Holds | Written |
|---|---|---|---|
| `remWh` | float | Energy left for the battery estimate | every 5 min if it moved 0.05 Wh |
| `distCal` | blob, up to 51 bytes | Distance calibration points (version, count, pairs of meters and dBm, checksum) | on each `d` command |

The RQS keeps nothing in flash.

### Size and cost (measured 2026-11-01 from the firmware's JSON in the PC tests, on top of the 417 byte record of 2026-10-31)

| Item | Amount |
|---|---|
| History record | about 637 bytes (about 661 stored); `latest` about 697 bytes |
| History growth | about 5.7 MB per day (one record per 10 s), so the 1 GB Spark limit lasts about 175 days |
| Live listener | about 30 MB per day per browser tab left open, about 0.9 GB per month |
| Pass schedule | about 1.6 to 1.8 KB per fetch, 4 a day, from GitHub Pages (no Firebase cost) |
| Spark limits | 100 simultaneous connections, 1 GB stored, 10 GB per month downloaded |

The history grows without bound. The website reads at most 24 h of it (an `orderByChild("ts")` query with
`limitToLast`), and v1 has no pruning job (no admin credential anywhere). Delete old history by hand in the Firebase
console before it nears 1 GB (roughly 5 to 6 months). See the main README for the steps.

## Placeholders still to measure

Search the sketch and `rqs.h` for `PLACEHOLDER`. The important ones:

| Value | What to do |
|---|---|
| `MUX_RDS_ON_OHMS`, `MUX_GAIN_T_MEASURED`, `MUX_GAIN_R_MEASURED` | Run `m`, compare each pin voltage to a multimeter at the sensor outputs |
| `BATT_USABLE_FRACTION` (0.90), `BATT_R_INT_OHM` (0.25) | Check after a full discharge cycle |
| `INA219_CURRENT_GAIN`, `INA219_CURRENT_OFFSET_MA` | Two-point calibration against a reference meter in series, then set `INA219_CALIBRATED` to 1 |
| `CAP_VS`, `CAP_VREF`, `CAP_T_COMP_US`, `CAP_TD_OFFSET_US`, `CAP_PF_SLOPE_CAL`, `CAP_PF_OFFSET_CAL`, `CAP_PRESS_DELTA_US` (27), `CAP_RELEASE_DELTA_US` (18) | Run `cal_capacitive` and paste what it prints |
| `THERM_R0`, `THERM_BETA`, `THERM_OFFSET_C` (96.2, the old CP3 offset, renamed from `BMP388_CALIBRATION_OFFSET`: it was always added to the thermistor, never to the BMP388) | Run `cal_thermistor` and paste what it prints |
| In `rqs.h`: `RQS_ALPHA_T_DB_PER_C` (0) | Warm-up test (`docs/rqs-validation.md`, test b) with the `w` CSV |
| In `rqs.h`: `RQS_K` (4), `RQS_D_FULL_DB` (12), `RQS_SIGMA_MIN_DB` (0.3), the weights (0.50, 0.20, 0.30), the label lines (15, 40, 70) and the rest of the table in `SPEC.md` 14.11 | Revisit after the NanoVNA test and a few real passes |

The old CP4 report numbers were mock values. Do not present any of them as acquired data.

## Known risks and open issues

- **TLS is not verified on the Firebase connections.** `setInsecure()` skips certificate checking on both. Pin the Google
  root CA before treating the uplink as trustworthy. (The pass schedule fetch does check its certificate.)
- **Credentials in flash.** `secrets.h` is compiled into the firmware image. Treat a lost board as a leaked password and
  rotate the Firebase user's password.
- **Strapping pin.** GPIO12 must be low at boot. If the board will not boot after the RF block transistor is wired, check this first.
- **The cap timing disables interrupts** for up to 2 ms per measurement on core 1. The tach ISR is on the same core, so a
  tach edge can be serviced a little late. The debounce window (2 ms) is shorter than any realistic pulse spacing, so the
  count stays right; resolution of the RPM reading is about 15 RPM over the 2 s window.
- **Sparse RF signal.** The RF reading is one broadband power number behind a 137 MHz SAW filter, not a spectrum. It sits near
  -75 dBm without a signal. The RQS works in dB above its own baseline, and its pass tag is evidence, not proof, that a rise
  is the satellite.
- **BMP388 position.** It sits elsewhere in the box, away from the AD8307 and the ESP32, so it reads box air, not the AD8307.
- **SAWbird 5 V supply** is the last open hardware task: Cole plans to power it from a stripped USB-C cable, then check
  the RF chain with a NanoVNA. Until then the RF reading stays flat and the RQS stays at NO SIGNAL.
- **Third TLS client.** The schedule fetch adds one, but it connects only every 6 hours and closes right away, so at most two
  TLS sessions are open at once, as before.

## First-run test checklist

1. Compile (1.2 verified with the versions above; 1.3 compiles the same way).
2. Flash with `FAN_ENABLED 0`, USB only (no 12 V). Confirm the LCD, I2C (BMP388 and INA219 lines in Serial), WiFi, and a
   successful sign-in line (`Firebase sign-in -> HTTP 200`).
3. Check the Firebase console: `/station/latest` appears and updates, `/station/history` grows by one record per 10 s,
   `/station/touch` changes on a press.
4. Rules test: try a write with no token (for example `curl -X PUT -d '{}' https://<host>/station/test.json`). It must be refused.
5. Power from the battery. Confirm INA219 voltage and current against the multimeter; run `f` right after a full charge.
6. Run `m` and the capacitive `b`, `s`, `c` commands; confirm press, release, the counter, and that each press moves the
   LCD to the next page and back to page 0 after page 4.
7. Do the tach check above, then set `FAN_ENABLED 1` and confirm a sane RPM that tracks fan speed. Done 2026-10-31:
   about 1,050 to 1,110 RPM on the 12 V battery, about 650 RPM on laptop USB power (USB is only used while flashing).
8. Pull the WiFi for a minute and plug it back in: uploads must resume without a reboot.
9. Leave it running for an hour: sign-in refreshes around the 50 minute mark without a gap in `/station/latest`.
10. RQS: after 5 minutes the LCD page 0 shows a score instead of "warming up". Run `q`: the clock should be set, and
    once `PASSES_URL` is in `secrets.h`, the schedule should show the next passes. Then do the three tests in
    `docs/rqs-validation.md` (NanoVNA, warm-up, a real pass).
11. Run the two calibration sketches and paste their `#define` lines into the station sketch.
