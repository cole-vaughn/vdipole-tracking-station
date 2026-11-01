// Battery health (SPEC section 14): pill text, detail rows and chart series, all from /station/health and
// /station/healthLog. Pure functions, so they are unit-tested. Nothing here makes up a number.
import { fmt, isNum, fmtHm, toUnit } from "./format.js";

// Bands (SPEC O12): 80 and above good (the USABC end-of-life line), 60 to 80 fair, under 60 replace.
// The 60 line is a project choice, not a cited one.
export function sohBand(pct, { goodAtOrAbovePct, replaceBelowPct }) {
  if (!isNum(pct)) return "none";
  if (pct >= goodAtOrAbovePct) return "good";
  if (pct >= replaceBelowPct) return "fair";
  return "replace";
}

export const BAND_WORDS = { good: "good", fair: "fair, less runtime", replace: "replace the pack", none: "" };

const CONF_WORDS = {
  high: "high",
  low: "low (wide plus-or-minus)",
  none: "none yet (learning)",
};

const LEARN_WORDS = {
  discharging: "learning from this discharge",
  charging: "charging (learning paused)",
  waiting: "waiting for a start point",
};

// The website shows at most 100 (a new pack can beat its label); the raw value stays in the log.
const shown = (pct) => Math.round(Math.min(100, pct));
const plusMinus = (sig) => (isNum(sig) ? Math.max(1, Math.round(sig)) : null);
const hasEstimate = (h) => !!h && (h.battSohConf === "high" || h.battSohConf === "low") && isNum(h.battSohPct);

export function pillView(h, { stationOffline = false } = {}, cfg) {
  if (!h || typeof h !== "object") {
    return { state: "none", text: "SOH --", est: false, band: "none", conf: "none", aria: "Battery health: no data yet. Show details." };
  }
  if (hasEstimate(h)) {
    const pct = shown(h.battSohPct), sig = plusMinus(h.battSohSigmaPct);
    const band = sohBand(h.battSohPct, cfg);
    const pm = sig === null ? "" : ` ±${sig}`;
    return {
      state: "estimate",
      text: `SOH ${pct}%${pm}`,
      est: true,
      band: stationOffline ? "none" : band,
      conf: h.battSohConf,
      aria: `Battery health about ${pct} percent${sig === null ? "" : `, plus or minus ${sig}`}, ${BAND_WORDS[band]}. ` +
        `Estimate, ${h.battSohConf} confidence${stationOffline ? ", station offline" : ""}. Show details.`,
    };
  }
  const prog = isNum(h.battSohProgressPct) ? Math.round(Math.min(100, Math.max(0, h.battSohProgressPct))) : null;
  return {
    state: "learning",
    text: prog === null ? "SOH learning" : `SOH learning ${prog}%`,
    est: false,
    band: "none",
    conf: "none",
    aria: `Battery health still learning${prog === null ? "" : `, ${prog} percent of the first discharge`}. Show details.`,
  };
}

// The newest log record that is a slice (not a reset marker), and the newest record of any kind.
export function lastRecords(records) {
  let newest = null, lastSlice = null;
  for (const r of records) {
    if (!r || !isNum(r.ts)) continue;
    if (!newest || r.ts > newest.ts) newest = r;
    if (r.why !== "reset" && (!lastSlice || r.ts > lastSlice.ts)) lastSlice = r;
  }
  return { newest, lastSlice };
}

function when(ts) {
  if (!isNum(ts)) return "--";
  const d = new Date(ts);
  return `${d.toLocaleDateString("en-US", { month: "short", day: "numeric" })} ${fmtHm(d)}`;
}

export function sliceText(r) {
  if (!r) return "--";
  if (r.ok) {
    const parts = [isNum(r.sliceSohPct) ? `${fmt(r.sliceSohPct, 1)}%` : null,
      isNum(r.e) ? `${fmt(r.e, 2)} Wh` : null, isNum(r.hours) ? `${fmt(r.hours, 1)} h` : null].filter(Boolean);
    return `used${parts.length ? `: ${parts.join(", ")}` : ""}${r.why ? ` (${r.why})` : ""}`;
  }
  return `not used: ${r.why || "no reason given"}`;
}

// Rows for the detail panel, as [label, value] pairs.
export function panelRows(h, records, cfg) {
  const { newest, lastSlice } = lastRecords(records);
  const rows = [];
  if (hasEstimate(h)) {
    const sig = plusMinus(h.battSohSigmaPct);
    const band = sohBand(h.battSohPct, cfg);
    rows.push(["Health", `${shown(h.battSohPct)}%${sig === null ? "" : ` ±${sig}`}, ${BAND_WORDS[band]}`]);
    const sigWh = isNum(h.battSohSigmaPct) ? (h.battSohSigmaPct / 100) * cfg.ratedWh : null;
    rows.push(["Usable energy", isNum(h.battCapWhEst)
      ? `${fmt(h.battCapWhEst, 1)}${sigWh === null ? "" : ` ±${fmt(sigWh, 1)}`} Wh of ${cfg.ratedWh} Wh rated` : "--"]);
  } else if (h && typeof h === "object") {
    rows.push(["Health", isNum(h.battSohProgressPct)
      ? `learning, first discharge ${Math.round(h.battSohProgressPct)}% of the way to a first estimate` : "learning"]);
  } else {
    rows.push(["Health", "no data from the station yet"]);
  }
  const conf = h?.battSohConf;
  rows.push(["Confidence", CONF_WORDS[conf] ?? "--"]);
  rows.push(["Slices used", isNum(h?.battSohSlices) ? String(h.battSohSlices) : "--"]);
  rows.push(["Cycles", isNum(h?.battCycles) ? `${fmt(h.battCycles, 1)} equivalent full` : "--"]);
  rows.push(["Now", LEARN_WORDS[h?.battSohLearn] ?? "--"]);
  rows.push(["Last update", newest ? when(newest.ts) : "--"]);
  rows.push(["Last discharge", sliceText(lastSlice)]);
  return rows;
}

// Chart data, all time. The fitted line steps at each record and runs on to now; a reset breaks it.
export function healthSeries(records, nowMs) {
  const recs = records.filter((r) => r && isNum(r.ts)).sort((a, b) => a.ts - b.ts);
  const fit = [], hi = [], lo = [], full = [], rest = [], notUsed = [];
  const point = (x, y, s) => { fit.push({ x, y, s }); hi.push({ x, y: y + s }); lo.push({ x, y: y - s }); };
  // Carry the last value up to the break, so its step is drawn, then break the line.
  const gap = (x) => {
    const last = fit[fit.length - 1];
    if (last && last.y !== null) point(x, last.y, last.s);
    fit.push({ x, y: null }); hi.push({ x, y: null }); lo.push({ x, y: null });
  };
  for (const r of recs) {
    if (r.why === "reset") { gap(r.ts); notUsed.push(r); continue; }
    if (isNum(r.fitSohPct)) point(r.ts, r.fitSohPct, isNum(r.fitSigmaPct) ? r.fitSigmaPct : 0);
    else if (fit.length && fit[fit.length - 1].y !== null) gap(r.ts);
    if (r.ok && isNum(r.sliceSohPct)) (r.start === "rest" ? rest : full).push({ x: r.ts, y: r.sliceSohPct, r });
    if (!r.ok) notUsed.push(r);
  }
  const last = fit[fit.length - 1];
  if (last && last.y !== null && isNum(nowMs) && nowMs > last.x) point(nowMs, last.y, last.s);
  return { fit, hi, lo, full, rest, notUsed: notUsed.reverse(), count: recs.length, firstTs: recs[0]?.ts ?? null };
}

export function notUsedText(r, unit = "C") {
  if (r.why === "reset") return `${when(r.ts)}: learning reset (z)`;
  const t = toUnit(r.tempC, unit);
  const extra = [isNum(r.hours) ? `${fmt(r.hours, 1)} h` : null, t === null ? null : `${fmt(t, 0)} °${unit}`].filter(Boolean);
  return `${when(r.ts)}: ${r.why || "no reason given"}${extra.length ? ` (${extra.join(", ")})` : ""}`;
}
