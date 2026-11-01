# Reception quality score: validation on the real station

These are the tests that check the reception quality score (RQS, `SPEC.md` section 14) on real hardware. Only Cole
can run them. Every table here is empty on purpose. Copy this file to `docs/validation/` with the date in its name
(for example `docs/validation/2026-11-08-nanovna.md`) and fill in that copy, so this procedure stays clean.

Do not write a number here that was not measured. If a step could not be done, write what happened instead.

## Before any test

1. Flash firmware 1.2 (`firmware/README.md`). Open the Serial Monitor at 115200 baud.
2. Wait at least 5 minutes after the start. The LCD's first page changes from "RQS warming up" to a score.
3. Send `q`. Check, and note in your copy:
   - "Clock" shows the right UTC time (NTP works).
   - "Schedule" says `ok` with some passes, if `PASSES_URL` is set. If it says "none loaded", the score is tagged
     NO SCHEDULE; tests (a) and (b) still work, test (c) needs the schedule.
   - "Temperature (BMP388)" shows a sensible box temperature, not "TEMP N/A".

## (a) NanoVNA CW test (does the score respond to a real signal?)

What it shows: a signal at 137.5 MHz raises the RF reading, the excess, and the score, and closer gives more.
What it does not show: an absolute calibration. The NanoVNA's output level at this frequency and the coupling of an
open cable end in the near field are not known well, so the dB numbers are only comparable within this one session.

Setup:

1. NanoVNA as a signal source: set start = stop = 137.5 MHz (a single frequency, so its output is a steady carrier).
2. A cable on its CH0 (port 1) with the far end open, no antenna on it. Keep the cable and the NanoVNA position the same
   for every distance; only move the open end.
3. Station antenna in its normal place. SAWbird powered.
4. Turn the NanoVNA output off (or disconnect it) and let the station run at least 15 minutes, so the baseline is the
   room's noise floor. Send `t` to turn on TEST MODE (the tag reads TEST; the pass part is fixed at half marks, so the
   score does not depend on where the satellite is).

For each distance: hold the open cable end at that distance from the antenna for 2 minutes, then read the Serial
print (or the website) and write down the values. Wait 2 minutes with the NanoVNA off between distances, so the
detection clears. The baseline does not take in samples while a detection is on, so it stays at the room's level.

| Distance | rfRawDbm (dBm) | rfFiltDbm (dBm) | rfExcessDb (dB) | Detected? | RQS | Label |
|---|---|---|---|---|---|---|
| NanoVNA off | | | | | | |
| 5 m | | | | | | |
| 3 m | | | | | | |
| 1 m | | | | | | |
| NanoVNA off again | | | | | | |

Notes (cable, NanoVNA settings, anything odd):

Afterwards send `t` again to turn TEST MODE off (a restart also turns it off).

What to look for: the excess and the score should rise as the cable gets closer, and fall back to near 0 and NO SIGNAL
with the source off. If the reading does not move at all, check the SAWbird supply and the RF chain first (`m` in the
Serial Monitor shows the RF channel voltage).

## (b) Warm-up test (fits ALPHA_T, the temperature compensation)

What it shows: how much the RF chain's reading moves with temperature, when the input power does not change. The
datasheet promises the AD8307 itself drifts little (`SPEC.md` 14.4), but the gain stage, the mux and the ESP32 ADC
are in the path too, and this test measures all of them together. The temperature is the BMP388's box air, not the
AD8307 chip, so the fit is "reading against box temperature".

Setup:

1. Replace the antenna with a 50 ohm terminator at the station's RF input, so the input power is constant.
2. Start cold: leave the station unpowered overnight, or an hour in a cooler room. Lid on, normal fan.
3. Power on, wait for the Serial Monitor, send `w`. A line arrives every 2 s:
   `WARM,ms,rfFilt,rfRaw,tempRaw,tempSmooth,rfComp`. `tempSmooth` is `nan` for a rejected BMP388 sample.
4. Log for 2 hours while it warms up. Save the Serial Monitor output to a file (copy and paste is fine).
5. Send `w` again to stop.

Fit (a spreadsheet is fine):

1. Drop the first 5 minutes and every line where `tempSmooth` is `nan`.
2. Fit a straight line: `rfFilt = a + ALPHA_T x (tempSmooth - 25)`. Note the slope ALPHA_T (dB per C) and its
   standard error (spreadsheets call it the slope's standard error; in LINEST it is the second row).
3. If the temperature changed by less than about 5 C over the run, the fit cannot say much: write that down and
   repeat with a colder start.

| Item | Value |
|---|---|
| Date, place, start temperature | |
| Temperature range covered (C) | |
| Lines used | |
| ALPHA_T (dB per C) | |
| Standard error of ALPHA_T | |
| rfFilt change over the run (dB) | |

Decision: set `RQS_ALPHA_T_DB_PER_C` in `firmware/station_firmware/rqs.h` to the fitted slope only if the slope is
clearly larger than its standard error (at least 3 times). Otherwise leave it at 0 and write "not significant" here.
The sign: `rf_comp = rfFilt - ALPHA_T x (T - 25)`, so a reading that rises as the box warms gives a positive ALPHA_T.

## (c) A real pass

What it shows: whether the score sees Meteor-M2-4 on a real pass, and whether the reading follows the predicted
distance curve (the correlation).

1. Pick a pass with the highest elevation over 30 degrees from the website's "Upcoming passes" table.
2. Antenna up, station on at least 15 minutes before the pass, `PASSES_URL` set and `q` showing the schedule.
3. Leave it running through the pass and 10 minutes after.
4. Afterwards, on the website: Graphs, Radio, range 1 h. The detection log lists the detection (if any), and the RF
   chart shows the pass window shaded. Also note the values below from the Serial print or the history.

| Item | Value |
|---|---|
| Date, pass start and end (local), highest elevation | |
| Detection start (local), or "none" | |
| Detection length | |
| Peak excess (dB) | |
| Final pass correlation (`passCorr`) | |
| Peak RQS, label and tag | |
| Weather, antenna position, anything nearby that transmits | |

If nothing was detected, that is a real result too: write it down, with the highest elevation. A low pass, a weak
LNA supply or local noise can all hide the satellite.

## Where the results go

- Your filled copy goes in `docs/validation/`, one file per test session.
- If a result changes a constant (ALPHA_T, or later K, the weights or the label lines), change it in
  `firmware/station_firmware/rqs.h`, and note which file justified it in the constant's comment.
