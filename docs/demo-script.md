# Demo script: the reception quality score, live with a NanoVNA

About 10 minutes in front of people, plus about 25 minutes of setup beforehand. The NanoVNA plays the satellite: it
puts out a steady 137.5 MHz signal, far stronger than Meteor-M2-4, so the whole chain and the score can be shown
working live. Then the real pass of 2026-11-01 shows what the station found on the real satellite.

## What you need

- The station (battery charged), with the antenna on its RF input, arms at about 120 degrees.
- The laptop, with the website open and the ESP32 on USB (so test mode can be switched on).
- The NanoVNA with a cable on CH0. The far end of that cable stays **open**: nothing on it, it acts as a tiny antenna.
- WiFi for the station and the laptop.

## Setup (start 25 minutes before)

1. Put the station where it will stay for the demo and switch it on. Do not move it after this; it is learning the
   room's noise floor.
2. Plug the ESP32 into the laptop. Open a serial monitor at 115200 baud, either the Arduino IDE (Tools, Serial
   Monitor) or in a terminal:
   `arduino-cli monitor -p /dev/cu.usbserial-0001 -c baudrate=115200`
3. Open https://cole-vaughn.github.io/vdipole-tracking-station/ and check the data age (top bar) says a few seconds.
4. After 5 minutes, page 0 of the LCD and the site show a score instead of "warming up". Expect **NO SIGNAL**.
5. Set the NanoVNA to a single frequency, with its output off or the cable end far away for now:
   on the NanoVNA screen, STIMULUS, then CW FREQ, then 137.5M. (Or over USB: `sweep 137500000 137500000 1`.)
6. Send `t` in the serial monitor: the tag changes to **TEST**. This keeps the score independent of where the real
   satellite is, and it never stays on after a restart.

## The demo

**1. The station at rest (1 minute).** On the site: the gauge reads NO SIGNAL, the four bars are near zero, the RF
meter sits near the station's baseline. Say: "Every 2 seconds the station measures 137 MHz power, learns the noise
floor from the last 15 minutes, and scores what it hears from 0 to 100."

**2. The signal arrives (2 minutes).** Bring the open cable end to **5 m** from the antenna and hold it still.
Within about 10 seconds:

- the RF reading rises above the baseline and the threshold line (Graphs, Radio tab);
- after 3 readings in a row above the threshold, **Detection** turns on;
- the gauge climbs; the label changes to WEAK, GOOD or STRONG;
- a new row appears in the detection log (Graphs, Radio).

Open **"Why this score?"** and read one line of each part aloud: strength, detection, pass match (test mode: half
marks), health, temperature.

**3. Distance (3 minutes).** Move the cable end to **3 m**, then **1 m**, about 1 minute each. The excess and the
score go up as it gets closer, which shows the reading is physical, not a fixed animation. Note the numbers (the
table in `docs/rqs-validation.md`, test a) if there is time.

**4. Back to quiet (1 minute).** Move the cable end away (or turn the NanoVNA off). After 3 readings below the lower
line, detection clears and the score falls back to NO SIGNAL. The baseline did not take in the test signal while it
was detected, so it is still the room's floor.

**5. The real satellite (3 minutes).** Open `docs/validation/2026-11-01-real-pass.md`. Say:

- "On November 1 the station watched a 62 degree pass of Meteor-M2-4. It correctly reported no signal: the score
  stayed at NO SIGNAL while the tag said PASS."
- "Fitting the expected pass shape to every reading puts any satellite signal below about 0.13 dB."
- "The link budget explains why: the satellite delivers about -102 dBm, but the detector measures everything from
  DC to 500 MHz, so its floor is about 18 dB higher than the 137 MHz band alone. A 137 MHz filter in front of the
  detector is the fix, and the analysis predicts the same pass would then rise by 3 to 6 dB."
- Optional: the 3:40 PM burst in that file, which the station flagged as NO PASS (a signal with no satellite up).

**6. Finish.** Send `t` again to leave test mode (a restart also does it).

## If something goes wrong

| What you see | What to do |
|---|---|
| "Station offline" banner | Check the station's WiFi and power; it reconnects on its own within a minute |
| Score stuck at "WARMING UP" | It was restarted less than 5 minutes ago; wait |
| No rise at 5 m | Move closer (3 m, then 1 m); check the NanoVNA is on CW 137.5 MHz and the cable is on CH0 |
| Detection never clears | Move the cable end well away; it clears 6 s after the reading drops |
| Tag still says NO PASS or PASS | Test mode is off; send `t` (needs the USB serial monitor) |
