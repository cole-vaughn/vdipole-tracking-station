# Station firmware 1.0

Single-sketch firmware for the Satellite V-Dipole Tracking Station (ESP32 DOIT DevKit V1, ELEE 4230/6230).
It reads every sensor, drives the LCD, and uploads to Firebase Realtime Database.

**Status: compiles cleanly (2026-10-31), NOT yet run on hardware.** Walk through the test checklist at the bottom. Nothing here has been measured on the
finished enclosure, so every value marked `PLACEHOLDER` in the sketch is a guess until it is measured.

## Files

| File | Purpose |
|---|---|
| `station_firmware/station_firmware.ino` | The whole firmware. Open this folder in the Arduino IDE (the folder name must match the sketch name). |
| `station_firmware/secrets.example.h` | Template for credentials. Copy to `secrets.h` in the same folder and fill in. |
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
- Verified build (2026-10-31): arduino-esp32 core 3.3.12, Adafruit BMP3XX 2.1.6, Adafruit INA219 1.2.3,
  Adafruit BusIO 1.17.4, Adafruit Unified Sensor 1.1.15, LiquidCrystal 1.0.7. Board "DOIT ESP32 DEVKIT V1".
  Result: 1,091,180 bytes of flash (83%), 51,736 bytes of RAM (15%). The only warnings are inside the Adafruit INA219
  library itself.
- Command-line build (optional): `arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 firmware/station_firmware`
  (needs `secrets.h` in that folder). If arduino-cli says `LiquidCrystal.h` is missing, run
  `arduino-cli lib install "LiquidCrystal"`; the Arduino IDE already includes it.
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
| Fan tach | 19 | **New.** Yellow tach wire. Needs a 10k pull-up to 3.3 V (see below) |
| LCD RS, E, D4 to D7 | 27, 26, 25, 33, 14, 13 | 4-bit mode |

## Before connecting the fan tach wire (do this first)

A 3-pin PC fan has power (12 V), ground, and tach. The tach output is normally open collector: it pulls to ground and
otherwise floats. Some fans, or a board with its own pull-up, can put 12 V on that wire. 12 V on a GPIO destroys the ESP32.

1. Power the fan from 12 V with the tach wire **not** connected to anything.
2. Measure tach to ground with a multimeter. Expect about 0 V or a floating reading. If it reads anywhere near 12 V,
   stop: put a level shifter or a divider in between.
3. Only then connect it to GPIO19 with a 10k resistor from GPIO19 to 3.3 V. The firmware also enables the internal pull-up,
   which is not strong enough on its own for a clean edge.
4. Until the tach wire is connected, set `FAN_ENABLED` to `0` in the sketch. Otherwise the firmware reports 0 RPM and "stalled".

## Behaviour summary

- **Every 2 s:** read BMP388, thermistor and RF through the mux (Kalman filtered), INA219, update the battery estimate,
  compute fan RPM, refresh the shared snapshot.
- **Every 100 ms:** capacitive sensor. A confirmed press increments the counter and advances the LCD page. A press or
  release triggers an immediate write to `/station/touch`.
- **Upload task (core 0, 16 KB stack):** signs in to Firebase Auth, then writes `/station/latest` every 2 s,
  `/station/history` every 10 s, `/station/touch` on a press or release, and `/station/meta` once per boot. The
  capacitive timing runs on core 1 with interrupts off, so WiFi and TLS work cannot disturb it.
- **Battery:** energy counting in watt-hours against 26.6 Wh x 0.90, a voltage-based correction used only near full and
  near empty, a smoothed power average for the time left, and the remaining energy saved to flash every 5 minutes.
- **BMP388 missing:** not fatal. The sketch keeps running and the BMP388 fields are sent as `null`.

### LCD pages (a capacitive press goes to the next page)

| Page | Content |
|---|---|
| 0 | BMP388 temperature, pressure, altitude |
| 1 | Thermistor raw and filtered |
| 2 | RF raw and filtered (dBm) |
| 3 | Capacitive delay, delta, pressed state |
| 4 | Battery voltage, current, power, peak current |
| 5 | Battery percent, Wh left, time left |
| 6 | Fan RPM, WiFi and Firebase status |

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

All keys live under `/station`. `null` means "sensor unavailable"; NaN is never sent. `ts` is the Firebase server time in
milliseconds since the epoch (`{".sv":"timestamp"}`), so the website computes data age as `Date.now() - latest.ts`.

| Key | Unit | Source |
|---|---|---|
| `ts` | ms epoch | Server timestamp |
| `tempC` | deg C | BMP388 |
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

Other paths:

- `/station/history/<pushId>`: same fields as `latest`, one record every 10 s.
- `/station/touch`: `{ state, count, ts }`, written the moment the press state changes.
- `/station/meta`: `{ fw, bootTs }`, once per boot.

### Size and cost (measured from a representative payload, 420 bytes)

| Item | Amount |
|---|---|
| History growth | about 3.6 MB per day (one record per 10 s), so the 1 GB Spark limit lasts about 275 days |
| Live listener | about 18 MB per day per browser tab left open, about 0.54 GB per month |
| Spark limits | 100 simultaneous connections, 1 GB stored, 10 GB per month downloaded |

The history grows without bound. The website reads at most 24 h of it (an `orderByChild("ts")` query with
`limitToLast`), and v1 has no pruning job (no admin credential anywhere). Delete old history by hand in the Firebase
console before it nears 1 GB (roughly 7 to 9 months). See the main README for the steps.

## Placeholders still to measure

Search the sketch for `PLACEHOLDER`. The important ones:

| Value | What to do |
|---|---|
| `MUX_RDS_ON_OHMS`, `MUX_GAIN_T_MEASURED`, `MUX_GAIN_R_MEASURED` | Run `m`, compare each pin voltage to a multimeter at the sensor outputs |
| `BATT_USABLE_FRACTION` (0.90), `BATT_R_INT_OHM` (0.25) | Check after a full discharge cycle |
| `INA219_CURRENT_GAIN`, `INA219_CURRENT_OFFSET_MA` | Two-point calibration against a reference meter in series |
| `CAP_VS`, `CAP_VREF`, `CAP_T_COMP_US` | Measure the GPIO high level and CAP_REF with a meter |
| `CAP_TD_OFFSET_US`, `CAP_PF_SLOPE_CAL`, `CAP_PF_OFFSET_CAL` | Fit with known test capacitors (so `capPF` becomes meaningful) |
| `CAP_PRESS_DELTA_US` (27), `CAP_RELEASE_DELTA_US` (18) | Re-tune with `b` and `s` after the enclosure, longer wires and foil pad are in |
| `BMP388_CALIBRATION_OFFSET` (96.2) | CP3-era offset between the thermistor and the BMP388. Re-check with the mux in the path |

The old CP4 report numbers were mock values. Do not present any of them as acquired data.

## Known risks and open issues

- **TLS is not verified.** `setInsecure()` skips certificate checking on both Firebase connections. Pin the Google root CA
  before treating the uplink as trustworthy.
- **Credentials in flash.** `secrets.h` is compiled into the firmware image. Treat a lost board as a leaked password and
  rotate the Firebase user's password.
- **Strapping pin.** GPIO12 must be low at boot. If the board will not boot after the RF block transistor is wired, check this first.
- **The cap timing disables interrupts** for up to 2 ms per measurement on core 1. The tach ISR is on the same core, so a
  tach edge can be serviced a little late. The debounce window (2 ms) is shorter than any realistic pulse spacing, so the
  count stays right; resolution of the RPM reading is about 15 RPM over the 2 s window.
- **Sparse RF signal.** The RF reading is one broadband power number behind a 137 MHz SAW filter, not a spectrum. It sits near
  -75 dBm without a signal. Anything that visualizes "signal" should use dB above a rolling baseline.
- **SAWbird 5 V cable** measured 0 V and is unresolved at the time of writing. That is a hardware task, not a firmware one.
- **Not compiled.** See the top of this file.

## First-run test checklist

1. Compile (already verified with the versions above).
2. Flash with `FAN_ENABLED 0`, USB only (no 12 V). Confirm the LCD, I2C (BMP388 and INA219 lines in Serial), WiFi, and a
   successful sign-in line (`Firebase sign-in -> HTTP 200`).
3. Check the Firebase console: `/station/latest` appears and updates, `/station/history` grows by one record per 10 s,
   `/station/touch` changes on a press.
4. Rules test: try a write with no token (for example `curl -X PUT -d '{}' https://<host>/station/test.json`). It must be refused.
5. Power from the battery. Confirm INA219 voltage and current against the multimeter; run `f` right after a full charge.
6. Run `m` and the capacitive `b`, `s`, `c` commands; confirm press, release, and the counter.
7. Do the tach check above, then set `FAN_ENABLED 1` and confirm a sane RPM that tracks fan speed.
8. Pull the WiFi for a minute and plug it back in: uploads must resume without a reboot.
9. Leave it running for an hour: sign-in refreshes around the 50 minute mark without a gap in `/station/latest`.
