// TEST FIXTURE DATA SOURCE. Every value here is made up. It is never published:
// tests/serve.mjs serves it in place of site/js/source.js only for tests and `npm run dev:fixtures`.
// The page shows a "TEST FIXTURE DATA" bar the whole time it is in use.
//
// URL options: ?autoplay=0 stops the generated live updates (tests drive it through window.__fixture).
//
// The reception quality score and distance fields (SPEC 14.12, 16) come from made-up test-tower sessions below, in
// ground mode: the tower on at 3.2 m when the page opens, an earlier calibration walk (1, 5, 10 m), and a session at
// 15 m. Nothing here comes from the station.

const params = new URLSearchParams(location.search);

function addLabel() {
  const bar = document.createElement("div");
  bar.id = "fixture-label";
  bar.textContent = "TEST FIXTURE DATA: made-up values for development, not from the station";
  bar.setAttribute("role", "note");
  bar.style.cssText =
    "position:sticky;bottom:0;z-index:50;padding:8px 12px;text-align:center;font:600 14px/1.3 system-ui;" +
    "letter-spacing:.06em;background:repeating-linear-gradient(45deg,#3b2a00,#3b2a00 12px,#4a3500 12px,#4a3500 24px);" +
    "color:#ffd27a;border-top:2px solid #ffb454";
  document.body.append(bar);
}

// Small deterministic noise so screenshots are repeatable.
let seed = 42;
const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
const noise = (a) => (rand() - 0.5) * 2 * a;

// ---------- Made-up reception quality score and distance (ground mode) ----------
const T = Date.now();
const MIN = 60e3;
// Made-up tower sessions: start, length, distance in meters.
const SESSIONS = [
  { start: T - 8 * MIN, len: 60 * MIN, m: 3.2 },
  { start: T - 40 * MIN, len: 2 * MIN, m: 1 },
  { start: T - 38 * MIN, len: 3 * MIN, m: 5 },
  { start: T - 35 * MIN, len: 3 * MIN, m: 10 },
  { start: T - 3 * 60 * MIN, len: 10 * MIN, m: 15 },
];
const P1 = -25, N = 2.3, SIGMA = 2.5;       // made-up path loss: -25 dBm at 1 m, n = 2.3
const clamp01 = (x) => Math.min(1, Math.max(0, x));
const r1 = (x) => Math.round(x * 10) / 10, r2 = (x) => Math.round(x * 100) / 100;
const labelOf = (v) => (v < 15 ? "NO SIGNAL" : v < 40 ? "WEAK" : v < 70 ? "GOOD" : "STRONG");

function rqsFields(ts, tempC, withParts) {
  const base = -57.9 + 0.1 * Math.sin(ts / 5.4e6), sigma = 0.3;
  const on = SESSIONS.find((x) => ts >= x.start && ts <= x.start + x.len);
  let total = base + 0.2 + noise(0.2), dist = null, lo = null, hi = null;
  if (on) {
    const sig = P1 - 10 * N * Math.log10(on.m) + noise(1.2);   // a little fading wobble
    total = 10 * Math.log10(10 ** (sig / 10) + 10 ** (base / 10));
    const est = 10 ** ((P1 - sig) / (10 * N)), f = 10 ** (SIGMA / (10 * N));
    dist = r2(est); lo = r2(est / f); hi = r2(est * f);
  }
  const excess = total - base, detected = excess > 4 * sigma;
  const sSnr = clamp01(excess / 12), sDet = detected ? 1 : 0;
  const rqs = Math.min(100, 100 * (0.7 * sSnr + 0.3 * sDet));
  const f = {
    rfFiltDbm: r2(total), rqs: r1(rqs), rqsLabel: labelOf(rqs), rqsTag: "GROUND",
    rfExcessDb: r2(excess), rfBaselineDbm: r2(base), rfThresholdDbm: r2(base + 4 * sigma), rfSigmaDb: sigma,
    rfDetected: detected, passElDeg: null, passCorr: null, nextPassEpoch: null, rqsTempC: r2(tempC),
    distM: dist, distLoM: lo, distHiM: hi,
  };
  if (withParts) Object.assign(f, { rqsSnr: r2(sSnr), rqsDet: sDet, rqsPass: null, rqsHealth: 1, distCal: 3, distN: N });
  return f;
}

function makeRecord(ts, latest = false) {
  const t = ts / 1000;
  const therm = 22.4 + Math.sin(t / 3000) * 0.8 + noise(0.3);
  const tempC = 22.1 + Math.sin(t / 3000) * 0.7 + noise(0.05);
  const q = rqsFields(ts, tempC, latest);
  return {
    ts,
    tempC,
    pressHPa: 991.2 + Math.sin(t / 9000) * 1.5 + noise(0.05),
    altM: 186 - Math.sin(t / 9000) * 12 + noise(0.4),
    thermRaw: therm,
    thermFilt: therm - noise(0.1),
    rfRawDbm: r2(q.rfFiltDbm + noise(0.8)),
    capTdUs: 610 + noise(1.5),
    capDeltaUs: noise(1.5),
    capPF: 410 + noise(1),
    touched: false,
    touchCount: 3,
    capFault: false,
    battV: 11.62 + noise(0.01),
    battmA: 412 + noise(10),
    battmW: 4790 + noise(110),
    battSocPct: 64.2 + ((Date.now() - ts) / 3600e3) * 0.4, // drains about 0.4% per hour
    battWhLeft: 15.37,
    battHoursLeft: 3.21,
    battPeakmA: 655,
    fanRpm: 1080 + noise(25),
    fanStalled: false,
    rssi: -61 + Math.round(noise(2)),
    uptimeS: 3 * 3600 + 720,
    ...q,
  };
}

function makeHistory(now) {
  const out = [];
  const step = 10000;
  for (let ts = now - 24 * 3600e3; ts <= now; ts += step) {
    // A 15 minute stretch where the station was off, so the charts show a real gap.
    if (ts > now - 5 * 3600e3 && ts < now - 5 * 3600e3 + 15 * 60e3) continue;
    const r = makeRecord(ts);
    // The fan tach was not wired until 8 hours ago: RPM is null before that.
    if (ts < now - 8 * 3600e3) r.fanRpm = null;
    out.push(r);
  }
  return out;
}

export function createSource() {
  const subs = {
    latest: new Set(), touch: new Set(), meta: new Set(), conn: new Set(), offset: new Set(),
  };
  const now = Date.now();
  const state = {
    latest: makeRecord(now, true),
    touch: { state: false, count: 3, ts: now - 140e3 },
    meta: { fw: "fixture", bootTs: now - (3 * 3600 + 720) * 1000, rqsAlphaT: 0, mode: "ground" },
    connected: true,
    offset: 0,
    history: makeHistory(now),
  };
  const emit = (k, v) => subs[k].forEach((cb) => cb(v));
  const sub = (k, value) => (cb) => { subs[k].add(cb); queueMicrotask(() => cb(value())); return () => subs[k].delete(cb); };

  let timer = null;
  function autoplay(on) {
    clearInterval(timer);
    if (!on) return;
    timer = setInterval(() => {
      const r = makeRecord(Date.now(), true);
      r.uptimeS = state.latest.uptimeS + 2;
      api.setLatest(r);
    }, 2000);
  }

  const api = {
    setLatest(patch, { ageMs = 0, replace = false } = {}) {
      const base = replace ? {} : state.latest;
      state.latest = { ...base, ...patch, ts: patch.ts ?? Date.now() - ageMs };
      emit("latest", state.latest);
    },
    setTouch(t) { state.touch = { ...state.touch, ...t, ts: t.ts ?? Date.now() }; emit("touch", state.touch); },
    setConnected(c) { state.connected = c; emit("conn", c); },
    setHistory(records) { state.history = records; },
    clearLatest() { state.latest = null; emit("latest", null); },
    setMeta(m) { state.meta = m; emit("meta", m); },
    autoplay,
    get state() { return state; },
  };
  window.__fixture = api;

  if (document.body) addLabel(); else addEventListener("DOMContentLoaded", addLabel);
  autoplay(params.get("autoplay") !== "0");

  return {
    label: "TEST FIXTURE (not real data)",
    onLatest: sub("latest", () => state.latest),
    onTouch: sub("touch", () => state.touch),
    onMeta: sub("meta", () => state.meta),
    onConnection: sub("conn", () => state.connected),
    onClockOffset: sub("offset", () => state.offset),
    async fetchHistory(sinceMs, limit) {
      return state.history.filter((r) => r.ts >= sinceMs).slice(-limit);
    },
    pause() {},
    resume() {},
  };
}
