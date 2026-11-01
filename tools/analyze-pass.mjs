// Looks for a satellite pass in logged station readings (docs/rqs-validation.md, test c).
// Fits reading(t) = a + b*t + A*shape(t), where shape is the received power relative to its peak, from the SGP4
// elevation and free-space loss (0 below the horizon). Then repeats the same fit with the pass shape moved to times
// when no satellite was up, to see how big a "bump" noise alone makes.
//   node tools/analyze-pass.mjs <folder with station-readings-*.csv and meteor-m2-4-tle.txt> <rise ISO> <set ISO>
//   node tools/analyze-pass.mjs docs/validation/data/2026-11-01 2026-11-01T20:14:25Z 2026-11-01T20:29:41Z
import { readFileSync, readdirSync } from "node:fs";
const S = process.argv[2];
const satlib = await import("../site/vendor/satellite.lib.js");
const { createTracker } = await import("../site/js/lib/orbit.js");
const { CONFIG } = await import("../site/js/config.js");
const l = readFileSync(S + "/meteor-m2-4-tle.txt", "utf8").trim().split(/\r?\n/).map((x) => x.trim());
const tr = createTracker({ name: l[0], tle1: l[1], tle2: l[2] }, CONFIG.station, satlib);
const rows = new Map();
for (const f of readdirSync(S).filter((n) => n.startsWith("station-readings") && n.endsWith(".csv"))) {
  const lines = readFileSync(`${S}/${f}`, "utf8").trim().split("\n");
  const h = lines[0].split(",");
  for (const line of lines.slice(1)) {
    const v = line.split(","), r = Object.fromEntries(h.map((k, i) => [k, v[i]]));
    if (r.rqsLabel === "WARMING UP" || r.rfRawDbm === "") continue;
    rows.set(+r.ts, { ts: +r.ts, raw: +r.rfRawDbm, filt: +r.rfFiltDbm, base: +r.rfBaselineDbm, corr: r.passCorr, tag: r.rqsTag });
  }
}
const data = [...rows.values()].sort((a, b) => a.ts - b.ts);
const re = 6371, rs = 6371 + 820;
const shapeAt = (ts) => {
  const el = tr.positionAt(new Date(ts)).elDeg;
  if (el <= 0) return 0;
  const e = (el * Math.PI) / 180, r = Math.sqrt(rs * rs - (re * Math.cos(e)) ** 2) - re * Math.sin(e);
  return (820 / r) ** 2;   // 1 straight overhead; this pass peaks at 62 deg
};
const peakShape = Math.max(...data.map((d) => shapeAt(d.ts)));
// Kalman lag for the filtered reading: run the template through the firmware's filter (Q 0.01, R 1.0).
function lagged(xs) { let est = xs[0], p = 0.0916; return xs.map((z) => { p += 0.01; const k = p / (p + 1); est += k * (z - est); p *= 1 - k; return est; }); }
function fit(ys, shape, t) {   // least squares for [1, t, shape]
  const X = ys.map((_, i) => [1, t[i], shape[i]]);
  const A = [[0,0,0],[0,0,0],[0,0,0]], bv = [0,0,0];
  X.forEach((x, i) => { for (let r = 0; r < 3; r++) { bv[r] += x[r] * ys[i]; for (let c = 0; c < 3; c++) A[r][c] += x[r] * x[c]; } });
  const det = (m) => m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
  const D = det(A); const sol = [0,1,2].map((k) => det(A.map((row, r) => row.map((v, c) => (c === k ? bv[r] : v)))) / D);
  return sol;
}
function run(label, from, to, key) {
  const d = data.filter((x) => x.ts >= from && x.ts <= to);
  const t = d.map((x) => (x.ts - from) / 3600e3);
  let shape = d.map((x) => shapeAt(x.ts) / peakShape);
  if (key === "filt") shape = lagged(shape);
  const ys = d.map((x) => x[key]);
  const [a, b, A] = fit(ys, shape, t);
  const resid = ys.map((y, i) => y - (a + b * t[i] + A * shape[i]));
  const sd = Math.sqrt(resid.reduce((s, r) => s + r * r, 0) / (resid.length - 3));
  return { label, n: d.length, A, sd };
}
const aos = Date.parse(process.argv[3]), los = Date.parse(process.argv[4]);
const first = data[0].ts, last = data[data.length - 1].ts;
console.log(`readings used: ${data.length}, from ${new Date(first).toLocaleTimeString("en-US",{timeZone:"America/New_York"})} to ${new Date(last).toLocaleTimeString("en-US",{timeZone:"America/New_York"})}`);
for (const key of ["raw", "filt"]) {
  const real = run("real pass", first, last, key);
  // Null test: the same fit with the pass template moved to times when no satellite was up.
  const nulls = [];
  const win = los - aos;
  for (let shift = -150 * 60e3; shift <= 150 * 60e3; shift += 30e3) {
    const s0 = aos + shift, s1 = los + shift;
    if (s1 + 2 * 60e3 > aos && s0 - 2 * 60e3 < los) continue;          // overlaps the real pass
    if (s0 - 2 * 60e3 < first || s1 + 2 * 60e3 > last) continue;     // not enough data around it
    const d = data.filter((x) => x.ts >= s0 - 2 * 60e3 && x.ts <= s1 + 2 * 60e3 && (x.ts < aos - 60e3 || x.ts > los + 60e3));
    if (d.length < 100) continue;
    const t = d.map((x) => (x.ts - s0) / 3600e3);
    let shape = d.map((x) => shapeAt(x.ts - shift) / peakShape);
    if (key === "filt") shape = lagged(shape);
    nulls.push(fit(d.map((x) => x[key]), shape, t)[2]);
  }
  const m = nulls.reduce((s, v) => s + v, 0) / (nulls.length || 1);
  const nsd = Math.sqrt(nulls.reduce((s, v) => s + (v - m) ** 2, 0) / Math.max(1, nulls.length - 1));
  console.log(`${key.padEnd(4)}: pass bump at the 62 deg peak A = ${real.A >= 0 ? "+" : ""}${real.A.toFixed(3)} dB (reading scatter ${real.sd.toFixed(2)} dB); ` +
    `same fit at ${nulls.length} no-satellite times: mean ${m.toFixed(3)}, spread ${nsd.toFixed(3)} dB` + (nulls.length > 2 ? ` -> ${((real.A - m) / nsd).toFixed(1)} sigma` : ""));
}
const inPass = data.filter((x) => x.ts >= aos && x.ts <= los);
console.log(`station's own pass correlation at the end: ${[...inPass].reverse().find((x) => x.corr !== "")?.corr}`);
