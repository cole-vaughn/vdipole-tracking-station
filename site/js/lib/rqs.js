// Reception quality score (SPEC section 14): what the page shows for the score the station computes.
// Pure functions, so they are unit-tested. Nothing here works out a score of its own: every number comes from the
// station's record (or is "--" when the record does not have it).
import { fmt, isNum, toUnit } from "./format.js";

// Label lines, the same as rqs.h (RQS_LABEL_WEAK, _GOOD, _STRONG).
export const RQS_LINES = { weak: 15, good: 40, strong: 70 };

export const BANDS = {
  nosignal: { label: "NO SIGNAL", from: 0, to: RQS_LINES.weak },
  weak: { label: "WEAK", from: RQS_LINES.weak, to: RQS_LINES.good },
  good: { label: "GOOD", from: RQS_LINES.good, to: RQS_LINES.strong },
  strong: { label: "STRONG", from: RQS_LINES.strong, to: 100 },
};

export function bandFor(rqs) {
  if (!isNum(rqs)) return "none";
  if (rqs < RQS_LINES.weak) return "nosignal";
  if (rqs < RQS_LINES.good) return "weak";
  if (rqs < RQS_LINES.strong) return "good";
  return "strong";
}

const TAG_WORDS = {
  PASS: "in a predicted pass",
  "NO PASS": "no pass predicted now",
  "NO SCHEDULE": "no pass schedule",
  TEST: "test mode",
};

const clamp01 = (x) => Math.min(1, Math.max(0, x));
const hasRqs = (rec) => !!rec && typeof rec === "object" && "rqsLabel" in rec;

// Everything the hero instrument shows, from one /station/latest record.
export function rqsView(rec, { stationOffline = false } = {}) {
  const bars = [
    { id: "snr", label: "Strength", value: null },
    { id: "det", label: "Detection", value: null },
    { id: "pass", label: "Pass match", value: null },
    { id: "health", label: "Health", value: null },
  ];
  if (!hasRqs(rec)) {
    return {
      state: "none", text: "--", value: null, band: "none", realBand: "none", label: "--", tag: null, tagWords: "", bars,
      detected: false, tempNa: false, battNa: false, passElDeg: null, passCorr: null, nextPassMs: null,
      aria: "Reception quality score: no data yet.",
    };
  }
  const scored = isNum(rec.rqs);
  const fields = { snr: rec.rqsSnr, det: rec.rqsDet, pass: rec.rqsPass, health: rec.rqsHealth };
  for (const b of bars) b.value = scored && isNum(fields[b.id]) ? clamp01(fields[b.id]) : null;
  const band = scored ? bandFor(rec.rqs) : "none";
  const tag = typeof rec.rqsTag === "string" ? rec.rqsTag : null;
  const label = scored ? (BANDS[band]?.label ?? String(rec.rqsLabel)) : rec.rqsLabel === "WARMING UP" ? "WARMING UP" : "--";
  const text = scored ? String(Math.round(rec.rqs)) : "--";
  const words = tag ? TAG_WORDS[tag] ?? tag.toLowerCase() : "";
  return {
    state: scored ? "scored" : "warming",
    text,
    value: scored ? Math.min(100, Math.max(0, rec.rqs)) : null,
    band: stationOffline ? "none" : band,
    realBand: band,
    label,
    tag,
    tagWords: words,
    bars,
    detected: rec.rfDetected === true,
    tempNa: !isNum(rec.rqsTempC),
    battNa: !isNum(rec.battSocPct),
    passElDeg: isNum(rec.passElDeg) ? rec.passElDeg : null,
    passCorr: isNum(rec.passCorr) ? rec.passCorr : null,
    nextPassMs: isNum(rec.nextPassEpoch) ? rec.nextPassEpoch * 1000 : null,
    aria: scored
      ? `Reception quality score ${text} of 100, ${label}, ${words}${stationOffline ? ", station offline" : ""}.`
      : `Reception quality score: ${label === "WARMING UP" ? "warming up, learning the noise baseline" : "no score"}.`,
  };
}

// S-meter and rings level, 0 to 1, from the station's own excess (F4). 0 while it has none.
export function rfLevel(rec, fullScaleDb = 12) {
  return isNum(rec?.rfExcessDb) ? clamp01(rec.rfExcessDb / fullScaleDb) : 0;
}

const signed = (v, d = 1) => (isNum(v) ? `${v >= 0 ? "+" : ""}${v.toFixed(d)}` : "--");

// "Why this score?" in plain words, as [title, text] pairs. meta is /station/meta (rqsAlphaT), unit is C or F.
export function whyLines(rec, meta, unit = "C") {
  const out = [];
  if (!hasRqs(rec)) {
    out.push(["No score yet", "The station has not sent a reception quality score. It needs firmware 1.2 or later."]);
    return out;
  }
  out.push(["What it is",
    "A 0 to 100 score the station works out every 2 s from its radio reading: how far the reading is above the usual " +
    "noise, whether that counts as a detection, whether it matches a predicted satellite pass, and the station's own health."]);
  if (!isNum(rec.rqs)) {
    out.push(["Warming up", "After a start, the station spends 5 minutes learning the noise baseline before it scores anything."]);
  } else {
    const k = isNum(rec.rfThresholdDbm) && isNum(rec.rfBaselineDbm) && isNum(rec.rfSigmaDb) && rec.rfSigmaDb > 0
      ? Math.round((rec.rfThresholdDbm - rec.rfBaselineDbm) / rec.rfSigmaDb) : null;
    out.push(["Strength", `${signed(rec.rfExcessDb)} dB above the noise baseline (${fmt(rec.rfBaselineDbm, 1)} dBm, the ` +
      "20th percentile of the last 15 minutes). Full marks at 12 dB. Worth half the score."]);
    out.push(["Detection", `${rec.rfDetected ? "On" : "Off"}. It turns on after 3 readings in a row above the threshold ` +
      `(${fmt(rec.rfThresholdDbm, 1)} dBm${k === null ? "" : `, ${k} times the noise spread above the baseline`}), and ` +
      "off after 3 readings back below a slightly lower line. Worth a fifth of the score."]);
  }
  const passWhy = {
    PASS: `Meteor-M2-4 should be overhead now${isNum(rec.passElDeg) ? `, at about ${Math.round(rec.passElDeg)}° elevation` : ""}. ` +
      "The score checks whether the signal rises and falls with the satellite's distance, as free-space path loss predicts: " +
      (isNum(rec.passCorr) ? `correlation ${rec.passCorr.toFixed(2)} so far (1 is a perfect match).` : "not known yet, it needs 20 s of readings."),
    "NO PASS": "No satellite pass is predicted right now. A strong signal outside a pass window can score at most 70, " +
      "because it is more likely interference than the satellite.",
    "NO SCHEDULE": "The station has no usable pass schedule (its clock is not set yet, or the schedule file is missing or " +
      "out of date), so the pass part gets half marks. The most it can score is 85.",
    TEST: "Test mode is on at the station, for a bench test with a signal source. The pass part is fixed at half marks.",
  };
  out.push(["Pass match", `${passWhy[rec.rqsTag] ?? "Unknown."} Worth three tenths of the score, and it only counts while a signal is detected.`]);
  if (isNum(rec.rqs)) {
    out.push(["Health", `Multiplies the score: lowered when the battery is under 20% or the box is over 45 °C.` +
      (isNum(rec.battSocPct) ? "" : " BATT N/A: the battery charge is unknown, so it does not lower the score.")]);
  }
  const alpha = meta && isNum(meta.rqsAlphaT) ? meta.rqsAlphaT : null;
  const comp = alpha === null ? "compensation unknown (older firmware)"
    : alpha === 0 ? "temperature compensation off (not measured yet)"
    : `temperature compensation on, ${alpha.toFixed(3)} dB per °C`;
  if (isNum(rec.rqsTempC)) {
    const t = toUnit(rec.rqsTempC, unit);
    out.push(["Temperature", `${t.toFixed(1)} °${unit}, smoothed over a minute. This is box air at the BMP388 board, not the ` +
      `RF detector chip itself, which runs a little warmer. ${comp[0].toUpperCase()}${comp.slice(1)}.`]);
  } else {
    out.push(["Temperature", "TEMP N/A: the BMP388 reading is missing or failed a check, so this reading gets no temperature " +
      "compensation and no heat penalty."]);
  }
  out.push(["Limits", "One broadband power number, not a spectrum: the score is evidence that a signal is the satellite, not proof."]);
  return out;
}

// Detection runs in history records (oldest first): consecutive records with rfDetected true. A gap between records
// longer than gapMs ends a run. Newest first. Detections shorter than the history step can be missed.
export function detectionLog(records, gapMs = 30e3) {
  const out = [];
  let cur = null, prevTs = null;
  const close = () => { if (cur) { cur.durationS = Math.max(0, (cur.endTs - cur.startTs) / 1000); out.push(cur); cur = null; } };
  for (const r of records) {
    if (!r || !isNum(r.ts)) continue;
    if (cur && prevTs !== null && r.ts - prevTs > gapMs) close();
    prevTs = r.ts;
    if (r.rfDetected === true) {
      if (!cur) cur = { startTs: r.ts, endTs: r.ts, peakExcess: null, peakRqs: null, inPass: false, ongoing: false };
      cur.endTs = r.ts;
      if (isNum(r.rfExcessDb) && (cur.peakExcess === null || r.rfExcessDb > cur.peakExcess)) cur.peakExcess = r.rfExcessDb;
      if (isNum(r.rqs) && (cur.peakRqs === null || r.rqs > cur.peakRqs)) cur.peakRqs = r.rqs;
      if (r.rqsTag === "PASS") cur.inPass = true;
    } else {
      close();
    }
  }
  if (cur) { cur.ongoing = true; close(); }
  return out.reverse();
}

// Time spans where pred(record) holds, for shading a chart: [[x0, x1], ...]. Gaps longer than gapMs split a span.
export function spans(records, pred, gapMs = 30e3) {
  const out = [];
  let start = null, last = null;
  for (const r of records) {
    if (!r || !isNum(r.ts)) continue;
    if (start !== null && r.ts - last > gapMs) { out.push([start, last]); start = null; }
    if (pred(r)) { if (start === null) start = r.ts; last = r.ts; }
    else if (start !== null) { out.push([start, r.ts]); start = null; }
    last = r.ts;
  }
  if (start !== null) out.push([start, last]);
  return out;
}

// Sparkline points for the last windowMs: [{x, y}] with null for gaps.
export function sparkPoints(records, nowMs, windowMs = 30 * 60e3, gapMs = 30e3) {
  const from = nowMs - windowMs;
  const out = [];
  let prev = null;
  for (const r of records) {
    if (!r || !isNum(r.ts) || r.ts < from) continue;
    if (prev !== null && r.ts - prev > gapMs) out.push({ x: prev + 1, y: null });
    out.push({ x: r.ts, y: isNum(r.rqs) ? r.rqs : null });
    prev = r.ts;
  }
  return out;
}
