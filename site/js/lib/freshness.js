import { isNum } from "./format.js";

// Station status from the age of the newest record.
// ts is Firebase server time (ms). offsetMs is the viewer's clock error (server minus local),
// from .info/serverTimeOffset, so a phone with a wrong clock still gets the right age.
export function freshness(ts, nowMs, offsetMs, { liveMs, offlineMs }) {
  if (!isNum(ts)) return { state: "nodata", ageMs: null };
  const ageMs = Math.max(0, nowMs + (isNum(offsetMs) ? offsetMs : 0) - ts);
  const state = ageMs < liveMs ? "live" : ageMs <= offlineMs ? "delayed" : "offline";
  return { state, ageMs };
}
