// Unit tests for the pure modules in site/js/lib. Run: npm run test:unit
import { test } from "node:test";
import assert from "node:assert/strict";
import * as satlib from "satellite.js";
import { CONFIG } from "../../site/js/config.js";
import { freshness } from "../../site/js/lib/freshness.js";
import { fmt, fmtAge, fmtHoursLeft, fmtTemp, fmtUptime, fmtCountdown } from "../../site/js/lib/format.js";
import { batteryBand, isLowBattery, packVoltsAtSoc } from "../../site/js/lib/battery.js";
import { createChargingDetector } from "../../site/js/lib/charging.js";
import { downsample, insertGaps, pickSeries } from "../../site/js/lib/series.js";
import { createTracker, footprintRadiusKm } from "../../site/js/lib/orbit.js";
import { tleChecksumOk } from "../../tools/fetch-orbit.mjs";

const T = { liveMs: 6000, offlineMs: 30000 };

test("freshness: live, delayed, offline, no data", () => {
  const now = 1_800_000_000_000;
  assert.equal(freshness(now - 2000, now, 0, T).state, "live");
  assert.equal(freshness(now - 6000, now, 0, T).state, "delayed");
  assert.equal(freshness(now - 30000, now, 0, T).state, "delayed");
  assert.equal(freshness(now - 30001, now, 0, T).state, "offline");
  assert.deepEqual(freshness(null, now, 0, T), { state: "nodata", ageMs: null });
});

test("freshness: corrects the viewer clock and never goes negative", () => {
  const now = 1_800_000_000_000;
  // Viewer clock is 20 s behind the server: offset +20 s. Record is 2 s old in server time.
  assert.equal(freshness(now + 18000, now, 20000, T).ageMs, 2000);
  assert.equal(freshness(now + 5000, now, 0, T).ageMs, 0);
});

test("format: null renders as --", () => {
  assert.equal(fmt(null, 2), "--");
  assert.equal(fmt(NaN), "--");
  assert.equal(fmtTemp(undefined, "C"), "--");
  assert.equal(fmtHoursLeft(null), "--");
  assert.equal(fmtAge(null), "--");
});

test("format: values", () => {
  assert.equal(fmtTemp(20, "F"), "68.0");
  assert.equal(fmtHoursLeft(3.5), "~3h 30m");
  assert.equal(fmtHoursLeft(150), ">99h");
  assert.equal(fmtAge(75_000), "1 min");
  assert.equal(fmtUptime(3 * 3600 + 120), "3 h 2 min");
  assert.equal(fmtCountdown(95 * 60e3), "01:35");
});

test("battery bands match SPEC D15", () => {
  const c = CONFIG.battery;
  assert.equal(batteryBand(100, 12.4, c), "green");
  assert.equal(batteryBand(36, 11.4, c), "green");
  assert.equal(batteryBand(35, 11.3, c), "amber");
  assert.equal(batteryBand(20, 11.2, c), "amber");
  assert.equal(batteryBand(19.9, 11.1, c), "red");
  assert.equal(batteryBand(80, 9.4, c), "red", "low voltage wins");
  assert.equal(batteryBand(null, 12, c), "none");
  assert.equal(isLowBattery(5, 10, c), true);
  assert.equal(isLowBattery(50, 11, c), false);
  assert.ok(Math.abs(packVoltsAtSoc(100, 3) - 12.6) < 1e-9);
  assert.ok(Math.abs(packVoltsAtSoc(0, 3) - 9.0) < 1e-9);
});

// Helper: feed the detector one sample every 2 s.
function run(det, start, seconds, sample) {
  let last;
  for (let s = 0; s < seconds; s += 2) last = det.update({ ts: start + s * 1000, ...sample(s) });
  return last;
}

test("charging: steady discharge stays discharging", () => {
  const det = createChargingDetector(CONFIG.charging);
  const v = packVoltsAtSoc(60, 3) - 0.3 * 0.25;
  const r = run(det, 0, 600, () => ({ battV: v, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "discharging");
});

test("charging: a short voltage spike does not flip the mode", () => {
  const det = createChargingDetector(CONFIG.charging);
  const v = packVoltsAtSoc(60, 3) - 0.075;
  const r = run(det, 0, 300, (s) => ({ battV: s >= 100 && s < 110 ? v + 0.5 : v, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "discharging");
});

test("charging: sustained excess enters, a brief dip does not exit, a clear drop exits", () => {
  const cfg = CONFIG.charging;
  const det = createChargingDetector(cfg);
  const v = packVoltsAtSoc(60, 3) - 0.075;
  let r = run(det, 0, 120, () => ({ battV: v, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "discharging");
  r = run(det, 120e3, 40, () => ({ battV: v + 0.4, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "charging");
  // Dip into the hysteresis band (above exit threshold) for 90 s, then back up: stays charging.
  r = run(det, 160e3, 90, () => ({ battV: v + 0.15, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "charging");
  // Charger unplugged: voltage back to normal for 90 s.
  r = run(det, 250e3, 90, () => ({ battV: v, battmA: 300, battSocPct: 60 }));
  assert.equal(r.state, "discharging");
});

test("charging: missing readings give unknown", () => {
  const det = createChargingDetector(CONFIG.charging);
  assert.equal(det.update({ ts: 0, battV: null, battmA: 300, battSocPct: 60 }).state, "unknown");
});

test("series: gaps for null and for missing time, downsample keeps spikes and gaps", () => {
  const recs = [];
  for (let i = 0; i < 9000; i++) recs.push({ ts: i * 10000, v: i === 4000 ? 99 : i >= 6000 && i < 6100 ? null : 1 });
  recs.splice(7000, 50); // the station went quiet for 500 s
  const pts = insertGaps(pickSeries(recs, "v"), 30000);
  const ds = downsample(pts, 1500);
  assert.ok(ds.length <= 1500, `length ${ds.length}`);
  assert.ok(ds.some((p) => p.y === 99), "spike kept");
  assert.ok(ds.filter((p) => p.y === null).length >= 2, "both gaps kept");
  assert.equal(ds[ds.length - 1].x, pts[pts.length - 1].x, "newest point kept");
  for (let i = 1; i < ds.length; i++) assert.ok(ds[i].x >= ds[i - 1].x, "time order");
});

// Vallado SGP4 verification case, satellite 00005 (from the published SGP4 test set).
const VALLADO = {
  name: "00005", noradId: 5,
  tle1: "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753",
  tle2: "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667",
};
const STATION = { lat: 33.92, lon: -84.84, altKm: 0.3 };

test("orbit: wrapper matches the SGP4 reference vector", () => {
  const tr = createTracker(VALLADO, STATION, satlib);
  const at = (min) => tr.eciAt(new Date(tr.epoch.getTime() + min * 60e3));
  // 10 m tolerance: JavaScript dates have 1 ms resolution, and the satellite moves about 7 m per ms.
  const close = (p, x, y, z) => assert.ok(Math.hypot(p.x - x, p.y - y, p.z - z) < 0.01, JSON.stringify(p));
  close(at(0), 7022.46529266, -1400.08296755, 0.03995155);
  close(at(360), -7154.03120202, -3783.17682504, -3536.19412294);
});

test("orbit: a satellite straight overhead has elevation near 90", () => {
  const sat = { name: "M2-4", tle1: "1 59051U 24039A   26303.94288269  .00000017  00000+0  27506-4 0  9991",
    tle2: "2 59051  98.7217 261.6501 0007768 130.9990 229.1861 14.22441657138564" };
  const t = new Date(Date.UTC(2026, 9, 31, 12, 0, 0));
  const sub = createTracker(sat, STATION, satlib).positionAt(t);
  const overhead = createTracker(sat, { lat: sub.lat, lon: sub.lon, altKm: 0 }, satlib).positionAt(t);
  assert.ok(overhead.elDeg > 89.5, `el ${overhead.elDeg}`);
  assert.ok(Math.abs(overhead.rangeKm - sub.altKm) < 2);
  assert.ok(sub.altKm > 780 && sub.altKm < 860, `alt ${sub.altKm}`);
});

test("orbit: next pass starts and ends at the horizon", () => {
  const sat = { name: "M2-4", tle1: "1 59051U 24039A   26303.94288269  .00000017  00000+0  27506-4 0  9991",
    tle2: "2 59051  98.7217 261.6501 0007768 130.9990 229.1861 14.22441657138564" };
  const tr = createTracker(sat, STATION, satlib);
  const pass = tr.nextPass(new Date(Date.UTC(2026, 9, 31, 0, 0, 0)), 24, 0);
  assert.ok(pass, "a polar orbiter passes within a day");
  assert.ok(Math.abs(tr.positionAt(pass.aos).elDeg) < 0.2);
  assert.ok(Math.abs(tr.positionAt(pass.los).elDeg) < 0.2);
  const minutes = (pass.los - pass.aos) / 60e3;
  assert.ok(minutes > 1 && minutes < 20, `duration ${minutes}`);
  assert.ok(pass.maxElDeg > 0 && pass.maxElDeg <= 90);
  assert.ok(Math.abs(footprintRadiusKm(820) - 3070) < 30);
});

test("orbit data: TLE checksums", () => {
  assert.equal(tleChecksumOk(VALLADO.tle1), true);
  assert.equal(tleChecksumOk(VALLADO.tle1.slice(0, 68) + "0"), false);
});

test("orbit: upcoming passes are in order and do not overlap", () => {
  const sat = { name: "M2-4", tle1: "1 59051U 24039A   26303.94288269  .00000017  00000+0  27506-4 0  9991",
    tle2: "2 59051  98.7217 261.6501 0007768 130.9990 229.1861 14.22441657138564" };
  const tr = createTracker(sat, STATION, satlib);
  const passes = tr.nextPasses(new Date(Date.UTC(2026, 9, 31, 0, 0, 0)), 5, 48, 0);
  assert.equal(passes.length, 5);
  for (let i = 0; i < passes.length; i++) {
    assert.ok(passes[i].aos < passes[i].los);
    if (i) assert.ok(passes[i].aos > passes[i - 1].los, "no overlap");
  }
});
