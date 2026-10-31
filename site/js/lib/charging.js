import { isNum } from "./format.js";
import { packVoltsAtSoc } from "./battery.js";

// Infers charging from the discharge-side sensor. Nothing on the station measures charging directly.
//
// Evidence that the pack is being charged:
//   1. excess: battery voltage sits above what a discharging pack would show at this charge and load
//      (open-circuit voltage for the estimated charge, minus the I x R sag);
//   2. rising: voltage climbs while the load current stays steady;
//   3. above: voltage higher than a 3S pack can reach while only discharging.
// Hysteresis: a lower threshold to stay in "charging" than to enter it, a hold time before any switch,
// and a minimum time in each state, so spikes and noise cannot flip the mode.
export function createChargingDetector(cfg) {
  let state = "discharging";
  let stateSince = null;
  let pendingSince = null;
  const recent = [];

  function slopeMvPerMin() {
    if (recent.length < 3) return null;
    const span = recent[recent.length - 1].t - recent[0].t;
    if (span < cfg.slopeWindowS * 1000 * 0.8) return null; // not enough history yet
    const n = recent.length;
    const mt = recent.reduce((a, r) => a + r.t, 0) / n;
    const mv = recent.reduce((a, r) => a + r.v, 0) / n;
    let num = 0, den = 0;
    for (const r of recent) { num += (r.t - mt) * (r.v - mv); den += (r.t - mt) ** 2; }
    return den > 0 ? (num / den) * 60000 * 1000 : null; // V per ms -> mV per min
  }

  function steadyLoad() {
    const ma = recent.map((r) => r.ma);
    return Math.max(...ma) - Math.min(...ma) <= cfg.steadyLoadMa;
  }

  function update({ ts, battV, battmA, battSocPct }) {
    if (![ts, battV, battmA, battSocPct].every(isNum)) {
      pendingSince = null;
      return { state: "unknown", excessV: null, slopeMvPerMin: null, evidence: false };
    }
    const last = recent[recent.length - 1];
    if (last && ts <= last.t) return { state, excessV: null, slopeMvPerMin: null, evidence: false };
    if (last && ts - last.t > cfg.maxGapS * 1000) { recent.length = 0; pendingSince = null; }

    recent.push({ t: ts, v: battV, ma: battmA });
    while (recent.length && ts - recent[0].t > cfg.slopeWindowS * 1000) recent.shift();
    if (stateSince === null) stateSince = ts;

    const expected = packVoltsAtSoc(battSocPct, cfg.cells) - (battmA / 1000) * cfg.rIntOhm;
    const excessV = battV - expected;
    const slope = slopeMvPerMin();
    const rising = slope !== null && slope >= cfg.riseMvPerMin && steadyLoad();
    const above = battV >= cfg.maxDischargeV;
    const threshold = state === "charging" ? cfg.exitExcessV : cfg.enterExcessV;
    const evidence = above || rising || excessV >= threshold;

    if ((state === "charging") === evidence) {
      pendingSince = null;
    } else {
      if (pendingSince === null) pendingSince = ts;
      const hold = (state === "charging" ? cfg.exitHoldS : cfg.enterHoldS) * 1000;
      if (ts - pendingSince >= hold && ts - stateSince >= cfg.minStateS * 1000) {
        state = evidence ? "charging" : "discharging";
        stateSince = ts;
        pendingSince = null;
      }
    }
    return { state, excessV, slopeMvPerMin: slope, evidence };
  }

  return { update };
}
