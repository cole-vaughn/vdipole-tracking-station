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
    fanRpm: 1840 + noise(25),
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

export function createSource() {
  const subs = { latest: new Set(), touch: new Set(), meta: new Set(), conn: new Set(), offset: new Set() };
  const now = Date.now();
  const state = {
    latest: makeRecord(now),
    touch: { state: false, count: 3, ts: now - 140e3 },
    meta: { fw: "fixture", bootTs: now - (3 * 3600 + 720) * 1000 },
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
