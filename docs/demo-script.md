# Demo script: the ground radio station, live with a NanoVNA "tower"

About 5 to 8 minutes in front of people, plus about 25 minutes of setup. The NanoVNA plays a radio tower: it puts out a
steady 137.5 MHz signal. The station scores what it hears, estimates how far away the tower is, and the website shows
it all live, including a 3D view with the tower at the station's own distance estimate.

## What you need

- The station (battery charged), firmware 1.3, antenna on its RF input, arms at about 120 degrees.
- The laptop on the same WiFi (your hotspot), the ESP32 on USB, the website open:
  https://cole-vaughn.github.io/vdipole-tracking-station/
- The NanoVNA with a cable on CH0, its far end **open** (nothing on it): that end is the tower's "antenna".
- A tape measure, to put the cable end at known distances from the station's antenna.

## Setup (start 25 minutes before)

1. Put the station where it stays for the demo and switch it on. Do not move it afterwards; it is learning the room's
   noise floor. The NanoVNA's output stays off (or the cable end far away) for now.
2. Open a serial monitor at 115200 baud with the ESP32 on USB: the Arduino IDE (Tools, Serial Monitor) or
   `arduino-cli monitor -p /dev/cu.usbserial-0001 -c baudrate=115200`
3. On the website, check the data age (top bar) is a few seconds and the score's tag says **GROUND**.
4. After 5 minutes the score reads **NO SIGNAL** (it has learned the floor).
5. Set the NanoVNA to one frequency: on its screen STIMULUS, CW FREQ, 137.5M (or over USB:
   `sweep 137500000 137500000 1`).
6. **Calibrate the distance** (about 5 minutes). For each distance, put the open cable end there, pointing the same way
   each time, wait about 40 s for the reading to settle, then type the command and press Enter:
   - at 1 m: `d 1`
   - at 5 m: `d 5`
   - at 10 m: `d 10`
   Each one prints the fit. Type `d` to see it again; `d x` starts over. The calibration is saved, so a restart keeps it.
7. Move the cable end well away again and wait for NO SIGNAL. Ready.

## The demo

**1. The station at rest (1 minute).** The 3D view shows the station and a faded tower labeled "no estimate". The gauge
reads NO SIGNAL. Say: "Every 2 seconds the station measures the power at 137 MHz, learns its own noise floor, and scores
what it hears from 0 to 100. Everything here is the station's own data, live from the cloud."

**2. The tower comes on (2 minutes).** Put the cable end at about **3 m**. Within about 10 seconds:

- the reading rises above the threshold (Graphs, Radio tab), detection turns on, the score climbs to STRONG;
- in the 3D view the tower lights up at the station's estimate, about 3 m, with its likely range shaded on the ground,
  and signal dots run from the tower to the station;
- the detection log adds a row with the distance.

Open **"Why this score?"** and read the Distance line aloud.

**3. Move it (2 minutes).** Walk the cable end to about **8 m**, then **2 m**, about 40 s each. The tower in the 3D view
follows, and the distance chart (Graphs, Radio) traces it. Say: "It only knows the signal strength. It works out the
distance from how fast the signal falls with distance, which it measured during setup. That is why it shows a range:
reflections in the room make the reading wobble by a few dB."

**4. Off (1 minute).** Turn the NanoVNA off. Detection clears after a few seconds, the score falls back to NO SIGNAL,
the tower fades to "no estimate". The baseline did not take in the test signal, so it is still the room's floor.

**5. The real satellite, if there is time (1 minute).** `docs/validation/2026-11-01-real-pass.md`: the station
watched a 62 degree Meteor-M2-4 pass, correctly reported no signal, and the analysis explains why (the detector's floor
is about 18 dB too high for a satellite) and what would fix it (a 137 MHz filter in front of the detector).

## If something goes wrong

| What you see | What to do |
|---|---|
| "Station offline" banner | Check the hotspot and the station's power; it reconnects on its own within a minute |
| Score stuck at "WARMING UP" | It restarted less than 5 minutes ago; wait |
| `d 3` says "No signal 3 dB clear" | The NanoVNA is off, on the wrong frequency, or too far; move closer and wait 40 s |
| Distance says "not calibrated" | Do step 6 of the setup |
| Distance jumps around | Keep the cable end still and pointed the same way; give it 40 s; people walking past change the reading |
| Tag says PASS / NO PASS instead of GROUND | The station is running satellite-mode firmware: flash 1.3 (`STATION_MODE_GROUND 1`) |
