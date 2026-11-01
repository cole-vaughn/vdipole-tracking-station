// Unit tests for site/js/lib/health.js (battery health, SPEC 14.14). Every record here is made up.
import { test } from "node:test";
import assert from "node:assert/strict";
import { CONFIG } from "../../site/js/config.js";
import { sohBand, pillView, panelRows, healthSeries, lastRecords, notUsedText } from "../../site/js/lib/health.js";

const cfg = CONFIG.soh;
const est = (over = {}) => ({
  ts: 1, battSohPct: 93.6, battSohSigmaPct: 4.6, battSohConf: "high", battCapWhEst: 24.9, battCycles: 3.24,
  battSohSlices: 4, battSohProgressPct: 100, battSohLearn: "discharging", inaCal: false, ...over,
});

test("health: bands match SPEC O12 (80 good, 60 replace line)", () => {
  assert.equal(sohBand(100, cfg), "good");
  assert.equal(sohBand(80, cfg), "good");
  assert.equal(sohBand(79.9, cfg), "fair");
  assert.equal(sohBand(60, cfg), "fair");
  assert.equal(sohBand(59.9, cfg), "replace");
  assert.equal(sohBand(null, cfg), "none");
});

test("health: pill text for every state", () => {
  assert.equal(pillView(null, {}, cfg).text, "SOH --");
  assert.equal(pillView(undefined, {}, cfg).band, "none");

  const high = pillView(est(), {}, cfg);
  assert.deepEqual([high.text, high.est, high.band, high.conf], ["SOH 94% ±5", true, "good", "high"]);
  assert.match(high.aria, /about 94 percent, plus or minus 5, good/);

  const low = pillView(est({ battSohPct: 71.2, battSohSigmaPct: 12.4, battSohConf: "low" }), {}, cfg);
  assert.deepEqual([low.text, low.band, low.conf], ["SOH 71% ±12", "fair", "low"]);

  const learning = pillView(est({ battSohPct: null, battSohSigmaPct: null, battSohConf: "none", battSohProgressPct: 41.6 }), {}, cfg);
  assert.deepEqual([learning.text, learning.est, learning.band], ["SOH learning 42%", false, "none"]);

  const offline = pillView(est({ battSohPct: 55 }), { stationOffline: true }, cfg);
  assert.equal(offline.band, "none", "grey while the station is offline");
  assert.equal(offline.text, "SOH 55% ±5", "the last value stays readable");

  assert.equal(pillView(est({ battSohPct: 104.2 }), {}, cfg).text, "SOH 100% ±5", "shows at most 100");
  assert.equal(pillView(est({ battSohSigmaPct: 0.3 }), {}, cfg).text, "SOH 94% ±1", "never claims ±0");
});

const log = [
  { ts: 1000, ok: true, start: "full", e: 12.1, sliceSohPct: 95.1, fitSohPct: 95.1, fitSigmaPct: 9.8, hours: 10.2 },
  { ts: 2000, ok: false, why: "too cold", fitSohPct: 95.1, fitSigmaPct: 9.8, tempC: 11, hours: 9.8 },
  { ts: 3000, ok: false, why: "reset" },
  { ts: 4000, ok: false, why: "no end point: current not steady", fitSohPct: null, fitSigmaPct: null },
  { ts: 5000, ok: true, start: "rest", e: 7.1, sliceSohPct: 90.4, fitSohPct: 90.4, fitSigmaPct: 6.6, hours: 6 },
];

test("health: detail rows", () => {
  const rows = Object.fromEntries(panelRows(est(), log, cfg));
  assert.equal(rows.Health, "94% ±5, good");
  assert.equal(rows["Usable energy"], "24.9 ±1.2 Wh of 26.6 Wh rated");
  assert.equal(rows.Confidence, "high");
  assert.equal(rows["Slices used"], "4");
  assert.equal(rows.Cycles, "3.2 equivalent full");
  assert.equal(rows.Now, "learning from this discharge");
  assert.equal(rows["Last discharge"], "used: 90.4%, 7.10 Wh, 6.0 h");

  const none = Object.fromEntries(panelRows(null, [], cfg));
  assert.equal(none.Health, "no data from the station yet");
  assert.equal(none["Last update"], "--");

  const learning = Object.fromEntries(panelRows(est({ battSohConf: "none", battSohPct: null, battSohProgressPct: 30 }), [], cfg));
  assert.match(learning.Health, /learning, first discharge 30%/);
  assert.equal(learning.Confidence, "none yet (learning)");
});

test("health: last records skip the reset marker for the last discharge", () => {
  const { newest, lastSlice } = lastRecords([...log, { ts: 6000, ok: false, why: "reset" }]);
  assert.equal(newest.ts, 6000);
  assert.equal(lastSlice.ts, 5000);
});

test("health: chart series break after a reset and run on to now", () => {
  const s = healthSeries(log, 9000);
  assert.equal(s.count, 5);
  assert.deepEqual(s.full.map((p) => p.y), [95.1]);
  assert.deepEqual(s.rest.map((p) => p.y), [90.4]);
  // 95.1 at 1000, again at 2000 (rejected slice, same fit), carried to the reset at 3000, then the break.
  assert.deepEqual(s.fit.map((p) => [p.x, p.y]), [[1000, 95.1], [2000, 95.1], [3000, 95.1], [3000, null], [5000, 90.4], [9000, 90.4]]);
  assert.equal(s.hi[0].y, 95.1 + 9.8);
  assert.equal(s.lo[4].y, 90.4 - 6.6);
  assert.deepEqual(s.notUsed.map((r) => r.ts), [4000, 3000, 2000], "newest first");
  assert.equal(healthSeries([], 9000).count, 0);
});

test("health: not-used text", () => {
  assert.match(notUsedText(log[1]), /too cold \(9\.8 h, 11 °C\)$/);
  assert.match(notUsedText(log[1], "F"), /52 °F\)$/);
  assert.match(notUsedText(log[2]), /learning reset \(z\)$/);
});
