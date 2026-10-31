import { isNum } from "./format.js";

// Turn records into {x, y} points for one field. y is null when the field is missing.
export function pickSeries(records, field, transform = (v) => v) {
  return records.map((r) => ({ x: r.ts, y: isNum(r[field]) ? transform(r[field]) : null }));
}

// Insert a null point wherever the station went quiet, so the chart draws a gap instead of a line.
export function insertGaps(points, maxGapMs) {
  const out = [];
  for (let i = 0; i < points.length; i++) {
    if (i > 0 && points[i].x - points[i - 1].x > maxGapMs) out.push({ x: points[i - 1].x + 1, y: null });
    out.push(points[i]);
  }
  return out;
}

// Largest-Triangle-Three-Buckets on one unbroken run of readings: one point per bucket, chosen to keep the
// visual shape (spikes included) without the up-down zigzag of min/max decimation. First and last are kept.
function lttb(run, target) {
  if (target >= run.length) return run;
  if (target < 3) return [run[0], run[run.length - 1]].slice(0, Math.max(1, target));
  const out = [run[0]];
  const every = (run.length - 2) / (target - 2);
  let a = 0;
  for (let i = 0; i < target - 2; i++) {
    const start = Math.floor(i * every) + 1;
    const end = Math.floor((i + 1) * every) + 1;
    const nEnd = Math.min(Math.floor((i + 2) * every) + 1, run.length);
    let ax = 0, ay = 0;
    for (let j = end; j < nEnd; j++) { ax += run[j].x; ay += run[j].y; }
    const n = nEnd - end;
    if (n > 0) { ax /= n; ay /= n; } else { ax = run[run.length - 1].x; ay = run[run.length - 1].y; }
    let best = start, bestArea = -1;
    for (let j = start; j < end; j++) {
      const area = Math.abs((run[a].x - ax) * (run[j].y - run[a].y) - (run[a].x - run[j].x) * (ay - run[a].y));
      if (area > bestArea) { bestArea = area; best = j; }
    }
    out.push(run[best]);
    a = best;
  }
  out.push(run[run.length - 1]);
  return out;
}

// Reduce to at most maxPoints. Gaps (null points) are kept as single nulls; each run of readings between gaps
// gets a share of the budget by length. The newest reading is always kept (it ends the last run).
export function downsample(points, maxPoints) {
  if (points.length <= maxPoints) return points;
  const parts = []; // arrays of readings, or null for a gap
  let run = [];
  for (const p of points) {
    if (p.y === null) {
      if (run.length) parts.push(run);
      run = [];
      if (parts[parts.length - 1] !== null) parts.push(null);
    } else {
      run.push(p);
    }
  }
  if (run.length) parts.push(run);
  const gaps = parts.filter((r) => r === null).length;
  const total = parts.reduce((n, r) => n + (r ? r.length : 0), 0);
  const budget = Math.max(2, maxPoints - gaps);
  const out = [];
  let lastX = null;
  for (const r of parts) {
    if (r === null) { out.push({ x: lastX === null ? points[0].x : lastX + 1, y: null }); continue; }
    const kept = lttb(r, Math.max(1, Math.floor((r.length / total) * budget)));
    out.push(...kept);
    lastX = kept[kept.length - 1].x;
  }
  return out;
}
