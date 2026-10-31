import { isNum } from "./format.js";

// Rolling RF baseline. The detector reads one broadband number (about -75 dBm with no signal),
// so visuals use "dB above baseline" instead of absolute dBm (SPEC F4).
// Baseline = a low percentile of rfFiltDbm over the window. While the reading is elevated, new samples
// are held out, so a pass does not raise its own baseline. The hold ends after maxHoldMin in case the
// noise floor really moved.
export function createRfBaseline(cfg) {
  const samples = [];
  let elevatedSince = null;

  function baseline() {
    if (samples.length < cfg.minSamples) return null;
    const sorted = samples.map((s) => s.dbm).sort((a, b) => a - b);
    return sorted[Math.min(sorted.length - 1, Math.floor(cfg.percentile * sorted.length))];
  }

  function add(ts, dbm) {
    if (!isNum(ts) || !isNum(dbm)) return;
    const last = samples[samples.length - 1];
    if (last && ts <= last.ts) return;
    const base = baseline();
    const elevated = base !== null && dbm - base >= cfg.holdAboveDb;
    if (elevated) {
      if (elevatedSince === null) elevatedSince = ts;
    } else {
      elevatedSince = null;
    }
    const holding = elevated && ts - elevatedSince < cfg.maxHoldMin * 60e3;
    if (!holding) samples.push({ ts, dbm });
    while (samples.length && ts - samples[0].ts > cfg.windowMin * 60e3) samples.shift();
  }

  // 0 to 1. Zero until a baseline exists, so nothing pulses on made-up assumptions.
  function level(dbm) {
    const base = baseline();
    if (base === null || !isNum(dbm)) return 0;
    return Math.min(1, Math.max(0, (dbm - base) / cfg.fullScaleDb));
  }

  function excessDb(dbm) {
    const base = baseline();
    return base === null || !isNum(dbm) ? null : dbm - base;
  }

  return { add, baseline, level, excessDb, get size() { return samples.length; } };
}
