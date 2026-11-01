// The calibration sketches copy circuit constants and measurement code from the station sketch (an Arduino sketch
// cannot include another sketch's file). These tests fail if a copy drifts from the station (SPEC 15.2).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const read = (p) => readFileSync(new URL(`../../firmware/${p}`, import.meta.url), "utf8");
const station = read("station_firmware/station_firmware.ino");
const sketches = { cal_capacitive: read("cal_capacitive/cal_capacitive.ino"), cal_thermistor: read("cal_thermistor/cal_thermistor.ino") };

// Single-line #defines, by name. (A multi-line macro ends its first line with a backslash; its next line is compared below.)
function defines(src) {
  const out = new Map();
  for (const m of src.matchAll(/^#define\s+(\w+)\s+(.*?)\s*(?:\/\/.*)?$/gm)) out.set(m[1], m[2].trim());
  return out;
}

// The text of a top-level function, from its signature line to the closing brace at the start of a line.
function fn(src, signature) {
  const i = src.indexOf(signature);
  if (i < 0) return null;
  return src.slice(i, src.indexOf("\n}\n", i) + 2);
}

const stationDefs = defines(station);

for (const [name, src] of Object.entries(sketches)) {
  test(`calibration: ${name} uses the station's constants`, () => {
    const defs = defines(src);
    let shared = 0;
    for (const [k, v] of defs) {
      if (!stationDefs.has(k)) continue;
      shared++;
      assert.equal(v, stationDefs.get(k), `${k} differs from station_firmware.ino`);
    }
    assert.ok(shared >= 8, `only ${shared} shared constants found`);
  });
}

test("calibration: the capacitive measurement code is the station's", () => {
  for (const sig of ["float capMeasureOnceUs() {", "float capMedianUs() {"]) {
    assert.equal(fn(sketches.cal_capacitive, sig), fn(station, sig), sig);
  }
});

test("calibration: the thermistor path is the station's", () => {
  for (const sig of ["void muxSelect(MuxChannel ch) {", "int muxReadCounts(MuxChannel ch) {", "float countsToPinVolts(int counts) {"]) {
    assert.equal(fn(sketches.cal_thermistor, sig), fn(station, sig), sig);
  }
  // The resistance half of thermistorVoltsToCelsius(), line by line.
  const steps = (src) => [...src.matchAll(/^\s*float (V_plus|k|V_ina_out|ina_gain|V_bridge|NodeB|NodeA|R_par|R_T) = .*$/gm)].map((m) => m[0].trim());
  assert.deepEqual(steps(fn(sketches.cal_thermistor, "float thermistorVoltsToOhms(float V_out) {")),
    steps(fn(station, "float thermistorVoltsToCelsius(float V_out) {")));
  // The second line of the multi-line mux gain macro.
  const gainLine = (src) => src.split("\n")[src.split("\n").findIndex((l) => l.startsWith("#define MUX_GAIN_THEORY")) + 1].trim();
  assert.equal(gainLine(sketches.cal_thermistor), gainLine(station));
});

test("calibration: the station still has every constant the sketches print", () => {
  for (const k of ["CAP_PRESS_DELTA_US", "CAP_RELEASE_DELTA_US", "CAP_VS", "CAP_VREF", "CAP_TD_OFFSET_US", "CAP_PF_SLOPE_CAL",
    "CAP_PF_OFFSET_CAL", "THERM_R0", "THERM_BETA", "THERM_OFFSET_C"]) {
    assert.ok(stationDefs.has(k), `${k} is missing from station_firmware.ino`);
  }
});
