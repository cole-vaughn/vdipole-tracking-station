// TEST FIXTURE DATA SOURCE. Every value here is made up. It is never published:
// tests/serve.mjs serves it in place of site/js/source.js only for tests and `npm run dev:fixtures`.
// The page shows a "TEST FIXTURE DATA" bar the whole time it is in use.
//
// URL options: ?autoplay=0 stops the generated live updates (tests drive it through window.__fixture).

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

function makeRecord(ts) {
  const t = ts / 1000;
  const rf = -75 + noise(0.6);
  const therm = 22.4 + Math.sin(t / 3000) * 0.8 + noise(0.3);
  return {
    ts,
    tempC: 22.1 + Math.sin(t / 3000) * 0.7 + noise(0.05),
    pressHPa: 991.2 + Math.sin(t / 9000) * 1.5 + noise(0.05),
    altM: 186 - Math.sin(t / 9000) * 12 + noise(0.4),
    thermRaw: therm,
    thermFilt: therm - noise(0.1),
    rfRawDbm: rf + noise(0.8),
    rfFiltDbm: rf,
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

// Made-up battery health, shaped like /station/health and /station/healthLog (SPEC 14.12). Three weeks of
// discharges with a `z` reset in the middle, so the chart shows a break, and a few rejected slices.
function makeHealth(now) {
  return {
    ts: now - 40e3, battSohPct: 93.1, battSohSigmaPct: 4.3, battSohConf: "high", battCapWhEst: 24.76,
    battCycles: 9.6, battSohSlices: 6, battSohProgressPct: 100, battSohLearn: "discharging", inaCal: false,
  };
}

function makeHealthLog(now) {
  const day = 86400e3;
  const ok = (d, start, slice, fit, sig, e, hours) =>
    ({ ts: now - d * day, ok: true, start, e, ah: e / 11.3, x: (e / 24.8) * 100, sx: 2.1, sliceSohPct: slice,
       fitSohPct: fit, fitSigmaPct: sig, tempC: 22, hours });
  const no = (d, why, fit, sig, extra = {}) =>
    ({ ts: now - d * day, ok: false, why, fitSohPct: fit, fitSigmaPct: sig, hours: 3.1, tempC: 21, ...extra });
  return [
    ok(20, "full", 95.1, 95.1, 9.8, 12.1, 10.2),
    ok(18, "rest", 91.0, 93.4, 7.9, 6.3, 5.4),
    no(17, "too cold", 93.4, 7.9, { tempC: 11, start: "full", hours: 9.8 }),
    ok(15, "full", 94.6, 93.9, 5.2, 18.5, 15.6),
    { ts: now - 13 * day, ok: false, why: "reset" },
    no(12, "no end point: current not steady", null, null, { start: "rest" }),
    ok(11, "full", 93.2, 93.2, 8.4, 11.6, 9.8),
    ok(9, "rest", 90.4, 92.1, 6.6, 7.1, 6.0),
    ok(7, "full", 94.0, 92.8, 5.1, 19.2, 16.3),
    no(5, "too small", 92.8, 5.1, { start: "full", hours: 0.4 }),
    ok(4, "full", 93.5, 93.0, 4.6, 21.9, 18.6),
    ok(2, "rest", 91.8, 92.8, 4.5, 8.8, 7.5),
    ok(1, "full", 94.4, 93.1, 4.3, 20.4, 17.3),
  ];
}

export function createSource() {
  const subs = {
    latest: new Set(), touch: new Set(), meta: new Set(), conn: new Set(), offset: new Set(), health: new Set(), healthLog: new Set(),
  };
  const now = Date.now();
  const state = {
    latest: makeRecord(now),
    touch: { state: false, count: 3, ts: now - 140e3 },
    meta: { fw: "fixture", bootTs: now - (3 * 3600 + 720) * 1000 },
    connected: true,
    offset: 0,
    history: makeHistory(now),
    health: params.get("health") === "none" ? null : makeHealth(now),
    healthLog: params.get("health") === "none" ? [] : makeHealthLog(now),
  };
  const emit = (k, v) => subs[k].forEach((cb) => cb(v));
  const sub = (k, value) => (cb) => { subs[k].add(cb); queueMicrotask(() => cb(value())); return () => subs[k].delete(cb); };

  let timer = null;
  function autoplay(on) {
    clearInterval(timer);
    if (!on) return;
    timer = setInterval(() => {
      const r = makeRecord(Date.now());
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
    setHealth(h) { state.health = h === null ? null : { ...state.health, ...h }; emit("health", state.health); },
    addHealthLog(r) { state.healthLog.push(r); emit("healthLog", r); },
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
    onHealth: sub("health", () => state.health),
    // Like onChildAdded: every existing record once, then each new one.
    onHealthLog(cb) {
      subs.healthLog.add(cb);
      queueMicrotask(() => state.healthLog.forEach((r) => cb(r)));
      return () => subs.healthLog.delete(cb);
    },
    async fetchHistory(sinceMs, limit) {
      return state.history.filter((r) => r.ts >= sinceMs).slice(-limit);
    },
    pause() {},
    resume() {},
  };
}
