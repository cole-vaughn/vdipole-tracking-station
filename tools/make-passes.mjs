// Writes site/data/passes.json: the next 48 hours of passes above 5 degrees for the station in site/js/config.js,
// from the orbit data in site/data/gp.json, with the same SGP4 code the website uses. The station fetches this file
// for the pass match of the reception quality score (SPEC.md 14.8). Run by the deploy workflow; by hand:
//   node tools/make-passes.mjs
import { readFileSync, writeFileSync } from "node:fs";
import * as satlib from "../site/vendor/satellite.lib.js";
import { CONFIG } from "../site/js/config.js";
import { createTracker } from "../site/js/lib/orbit.js";

const GP = new URL("../site/data/gp.json", import.meta.url);
const OUT = new URL("../site/data/passes.json", import.meta.url);

// Same limits as rqs.h (RQS_MAX_PASSES, RQS_MAX_EL), so the station can always hold the whole file.
export const LIMITS = { hours: 48, minEl: 5, stepS: 30, maxPasses: 24, maxPoints: 40, maxTleAgeDays: CONFIG.orbit.staleDays };

const round1 = (x) => Math.round(x * 10) / 10;

export function makeSchedule(gp, from, opts = {}) {
  const o = { ...LIMITS, ...opts };
  const want = CONFIG.satellites[0];
  const sat = gp.satellites.find((s) => s.noradId === want.noradId);
  if (!sat) throw new Error(`No orbit data for ${want.name} (${want.noradId}) in gp.json`);
  const tr = createTracker(sat, CONFIG.station, satlib);
  const ageDays = (from.getTime() - tr.epoch.getTime()) / 86400e3;
  // Old elements drift by kilometres a day; a wrong schedule is worse than none (the station then says NO SCHEDULE).
  if (ageDays > o.maxTleAgeDays) throw new Error(`Orbit data is ${ageDays.toFixed(1)} days old; no schedule made`);

  const passes = tr.nextPasses(from, o.maxPasses, o.hours, o.minEl).map((p) => {
    const aos = Math.round(p.aos.getTime() / 1000), los = Math.round(p.los.getTime() / 1000);
    const el = [];
    for (let t = aos; ; t += o.stepS) {
      const tt = Math.min(t, los);
      el.push(round1(Math.max(-5, tr.positionAt(new Date(tt * 1000)).elDeg)));
      if (tt === los || el.length === o.maxPoints) break;
    }
    // A pass longer than the station can hold (never seen: the longest is about 13 min) is cut at its last point.
    const end = Math.min(los, aos + (el.length - 1) * o.stepS);
    return { aos, los: end, maxEl: round1(Math.max(p.maxElDeg, ...el)), el };
  });
  const generated = Math.round(from.getTime() / 1000);   // same rounding as aos, so a pass in progress starts here
  return {
    source: `SGP4 (satellite.js) from ${gp.source ?? "CelesTrak GP data"}`,
    generated,
    validUntil: generated + o.hours * 3600,
    noradId: want.noradId,
    tleEpoch: tr.epoch.toISOString(),
    station: { lat: CONFIG.station.lat, lon: CONFIG.station.lon },
    minEl: o.minEl,
    step: o.stepS,
    passes,
  };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const gp = JSON.parse(readFileSync(GP, "utf8"));
  const doc = makeSchedule(gp, new Date());
  const text = JSON.stringify(doc);
  writeFileSync(OUT, text + "\n");
  console.log(`Wrote ${doc.passes.length} passes (${text.length} bytes) to site/data/passes.json`);
}
