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

// Min/max decimation: each bucket keeps its lowest and highest point (in time order), so spikes survive.
// A bucket with no readings keeps one null point, so gaps survive too.
export function downsample(points, maxPoints) {
  if (points.length <= maxPoints) return points;
  // Up to 3 points per bucket (min, max, gap), plus one slot for the newest point.
  const buckets = Math.max(1, Math.floor((maxPoints - 1) / 3));
  const size = points.length / buckets;
  const out = [];
  for (let b = 0; b < buckets; b++) {
    const start = Math.floor(b * size);
    const end = Math.min(points.length, Math.floor((b + 1) * size));
    let min = null, max = null, sawNull = false;
    for (let i = start; i < end; i++) {
      const p = points[i];
      if (p.y === null) { sawNull = true; continue; }
      if (!min || p.y < min.y) min = p;
      if (!max || p.y > max.y) max = p;
    }
    if (!min) { out.push({ x: points[start].x, y: null }); continue; }
    if (min === max) out.push(min);
    else if (min.x <= max.x) out.push(min, max);
    else out.push(max, min);
    if (sawNull) out.push({ x: points[end - 1].x, y: null });
  }
  // The live right edge must always show the newest reading.
  const newest = points[points.length - 1];
  if (out[out.length - 1] !== newest) out.push(newest);
  return out;
}
