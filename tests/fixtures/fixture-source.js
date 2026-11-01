// TEST FIXTURE DATA SOURCE. Every value here is made up. It is never published:
// tests/serve.mjs serves it in place of site/js/source.js only for tests and `npm run dev:fixtures`.
// The page shows a "TEST FIXTURE DATA" bar the whole time it is in use.
//
// URL options: ?autoplay=0 stops the generated live updates (tests drive it through window.__fixture).
//
// The reception quality score fields (SPEC 14.12) come from a made-up pass schedule below, not from orbit data and
// not from the station: a pass in progress when the page opens, earlier passes, a low pass where nothing was heard,
// and a burst of interference outside any pass (tagged NO PASS, so it can score at most 70).

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

// ---------- Made-up reception quality score ----------
const T = Date.now();
const MIN = 60e3;
// start, length, highest elevation, and the made-up signal: dB above the floor with the satellite overhead (null: none)
const PASSES = [
  { start: T - 6 * MIN, len: 12 * MIN, maxEl: 48, sig: 13 },
  { start: T - 190 * MIN, len: 11 * MIN, maxEl: 22, sig: 12 },
  { start: T - 13 * 60 * MIN, len: 12.5 * MIN, maxEl: 64, sig: 18 },
  { start: T - 18 * 60 * MIN, len: 6 * MIN, maxEl: 8, sig: null },
  { start: T + 95 * MIN, len: 9 * MIN, maxEl: 15, sig: 12 },
];
const BURST = { start: T - 9 * 60 * MIN, len: 4 * MIN, db: 14 };
const clamp01 = (x) => Math.min(1, Math.max(0, x));
const r1 = (x) => Math.round(x * 10) / 10, r2 = (x) => Math.round(x * 100) / 100;
// Free-space loss relative to overhead, the same formula as rqs.h (Re 6371 km, h 820 km).
function friisDb(elDeg) {
  const e = (elDeg * Math.PI) / 180, re = 6371, rs = 6371 + 820;
  return -20 * Math.log10((Math.sqrt(rs * rs - (re * Math.cos(e)) ** 2) - re * Math.sin(e)) / 820);
}
const labelOf = (v) => (v < 15 ? "NO SIGNAL" : v < 40 ? "WEAK" : v < 70 ? "GOOD" : "STRONG");

function rqsFields(ts, tempC, withParts) {
  const base = -75.2 + 0.15 * Math.sin(ts / 5.4e6), sigma = 0.3;
  const pass = PASSES.find((p) => ts >= p.start && ts <= p.start + p.len);
  let excess = noise(0.25), el = null, corr = null;
  if (pass) {
    el = 5 + (pass.maxEl - 5) * Math.sin((Math.PI * (ts - pass.start)) / pass.len);
    if (pass.sig !== null) excess = Math.max(excess, pass.sig + friisDb(el) + noise(0.25));
    const secs = (ts - pass.start) / 1000;
    if (secs >= 20) corr = pass.sig === null ? 0.12 : Math.min(0.93, 0.6 + secs / 900);
  } else if (ts >= BURST.start && ts <= BURST.start + BURST.len) {
    excess = BURST.db + noise(0.4);
  }
  const detected = excess > 4 * sigma;
  const sSnr = clamp01(excess / 12), sDet = detected ? 1 : 0;
  const sPass = pass ? 0.4 + 0.6 * clamp01(((corr ?? 0.3) - 0.3) / 0.6) : 0;
  const rqs = Math.min(100, 100 * (0.5 * sSnr + 0.2 * sDet + 0.3 * sPass * sDet));
  const next = PASSES.map((p) => p.start).filter((s) => s > ts).sort((a, b) => a - b)[0];
  const f = {
    rfFiltDbm: r2(base + excess), rqs: r1(rqs), rqsLabel: labelOf(rqs), rqsTag: pass ? "PASS" : "NO PASS",
    rfExcessDb: r2(excess), rfBaselineDbm: r2(base), rfThresholdDbm: r2(base + 4 * sigma), rfSigmaDb: sigma,
    rfDetected: detected, passElDeg: el === null ? null : r1(el), passCorr: corr === null ? null : r2(corr),
    nextPassEpoch: next ? Math.floor(next / 1000) : null, rqsTempC: r2(tempC),
  };
  if (withParts) Object.assign(f, { rqsSnr: r2(sSnr), rqsDet: sDet, rqsPass: r2(sPass), rqsHealth: 1 });
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
    meta: { fw: "fixture", bootTs: now - (3 * 3600 + 720) * 1000, rqsAlphaT: 0 },
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
