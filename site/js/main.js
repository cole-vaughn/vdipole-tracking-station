// Wires the data source to the page. Nothing here invents data: every value shown comes from the
// station (through the source) or from the public orbit file.
import { CONFIG } from "./config.js";
import { createSource } from "./source.js";
import { freshness } from "./lib/freshness.js";
import { fmtAge, fmtUptime, fmtClock, isNum } from "./lib/format.js";
import { createRfBaseline } from "./lib/rf.js";
import { createChargingDetector } from "./lib/charging.js";
import {
  initCards, renderBattery, renderRf, renderFan, renderTouch, renderEnv, touchView, flashTouch, setText,
} from "./ui/cards.js";
import { createGraphs } from "./ui/graphs.js";
import { startSatellite } from "./ui/satellite.js";

const $ = (id) => document.getElementById(id);

const app = {
  source: null,
  latest: null,
  touch: null,
  meta: null,
  connected: null,
  everConnected: false,
  offsetMs: 0,
  unit: loadUnit(),
  readError: null,
  lastError: null,
  sat: null,
  globe: null,
  charge: null,
  rfLevel: 0,
};
const now = () => Date.now() + app.offsetMs; // server-corrected time
const rf = createRfBaseline(CONFIG.rf);
const charging = createChargingDetector(CONFIG.charging);

function loadUnit() {
  try { return localStorage.getItem("tempUnit") === "F" ? "F" : "C"; } catch { return "C"; }
}
function saveUnit(u) {
  try { localStorage.setItem("tempUnit", u); } catch { /* storage blocked: the toggle still works for this visit */ }
}

function noteError(text) {
  app.lastError = `${fmtClock(new Date())} ${text}`;
  setText("d-error", app.lastError);
}

// ---------- Status bar, banner, diagnostics (every second, even with no new data) ----------
const LABELS = { live: "Live", delayed: "Delayed", offline: "Offline", nodata: "No data" };

function wifiText(rssi) {
  if (!isNum(rssi) || rssi === 0) return "--";
  const q = rssi >= -67 ? "good" : rssi >= -75 ? "fair" : "weak";
  return `${rssi} dBm ${q}`;
}

function lastSeenText(ts) {
  if (!isNum(ts)) return "--";
  const d = new Date(ts);
  const old = Date.now() - ts > 86400e3;
  return (old ? `${d.toLocaleDateString()} ` : "") + fmtClock(d);
}

function bannerFor(f) {
  if (!app.source) {
    return ["info", "Not connected to the station database yet. Live data starts once the database address is set in js/config.js."];
  }
  if (app.readError) return ["bad", `Cannot read station data: ${app.readError}. The page keeps retrying.`];
  if (!navigator.onLine) return ["warn", "Your device is offline. Data resumes when your connection returns."];
  if (f.state === "offline") {
    const d = new Date(app.latest.ts);
    return ["bad", `Station offline. Last data ${fmtAge(f.ageMs)} ago, at ${fmtClock(d)} local (${fmtClock(d, true)} UTC). Showing the last known values.`];
  }
  if (app.everConnected && app.connected === false) return ["warn", "Lost the link to the database. Reconnecting."];
  if (f.state === "nodata" && app.connected) return ["info", "No station data yet. The station has not sent a reading."];
  return null;
}

let lastBannerText = "";
function tick() {
  const f = freshness(app.latest?.ts, Date.now(), app.offsetMs, CONFIG.freshness);
  const status = $("status");
  status.dataset.state = f.state;
  setText("status-label", LABELS[f.state]);
  document.body.dataset.station = f.state;
  setText("age", f.ageMs === null ? "--" : fmtAge(f.ageMs));
  setText("uptime", fmtUptime(app.latest?.uptimeS));
  setText("wifi", wifiText(app.latest?.rssi));
  setText("last-seen", lastSeenText(app.latest?.ts));
  const d = new Date();
  setText("clock", `${fmtClock(d)} / ${fmtClock(d, true)} UTC`);

  const banner = $("banner");
  const b = bannerFor(f);
  if (b) {
    banner.dataset.kind = b[0];
    if (b[1] !== lastBannerText) { banner.textContent = b[1]; lastBannerText = b[1]; }
    banner.hidden = false;
  } else {
    banner.hidden = true;
    lastBannerText = "";
  }

  setText("d-source", app.source ? app.source.label : "not configured");
  setText("d-link", app.connected === null ? "--" : app.connected ? "connected" : "reconnecting");
  setText("d-age", f.ageMs === null ? "--" : `${(f.ageMs / 1000).toFixed(1)} s`);
  setText("d-offset", `${Math.round(app.offsetMs)} ms`);
  setText("d-fw", app.meta?.fw ?? "--");
  setText("d-tle", app.sat?.available ? fmtAge(app.sat.tleAgeMs) : "unavailable");
  setText("d-rf", rf.baseline() === null ? `learning (${rf.size} samples)` : `${rf.baseline().toFixed(1)} dBm`);
  const c = app.charge;
  setText("d-charge", c && c.state !== "unknown"
    ? `${c.state}${isNum(c.excessV) ? `, excess ${c.excessV.toFixed(2)} V` : ""}${isNum(c.slopeMvPerMin) ? `, slope ${c.slopeMvPerMin.toFixed(0)} mV/min` : ""}`
    : "--");

  // Battery goes grey while offline, so re-render it when the status changes.
  if (f.state !== tick.prevState) { tick.prevState = f.state; renderAll(); }
}

// ---------- Cards ----------
function renderAll() {
  const rec = app.latest;
  const stationOffline = document.body.dataset.station === "offline";
  renderBattery(rec, CONFIG.battery, { stationOffline, charge: app.charge, chargingCfg: CONFIG.charging });
  app.rfLevel = renderRf(rec, rf, app.sat?.available ? app.sat.pos.elDeg : null);
  renderFan(rec, CONFIG.fan);
  renderTouch(touchView(rec, app.touch));
  renderEnv(rec, app.unit);
}

function onLatest(rec) {
  app.readError = null;
  app.latest = rec && typeof rec === "object" ? rec : null;
  if (app.latest) {
    rf.add(app.latest.ts, app.latest.rfFiltDbm);
    app.charge = charging.update(app.latest);
    graphs.addLive(app.latest);
  }
  renderAll();
  tick();
}

function onTouch(t) {
  const prev = app.touch;
  app.touch = t && typeof t === "object" ? t : null;
  // React to a new press only, not to the value that arrives when the page first loads.
  const pressed = prev && app.touch && app.touch.state === true && (prev.state !== true || app.touch.count !== prev.count);
  renderTouch(touchView(app.latest, app.touch));
  if (pressed) {
    flashTouch();
    app.globe?.ripple();
  }
}

function onReadError(err) {
  app.readError = err?.code === "PERMISSION_DENIED" ? "permission denied by the database rules" : err?.message || "unknown error";
  noteError(`Read: ${app.readError}`);
  tick();
}

// ---------- Controls ----------
const unitBtn = $("unit-toggle");
function showUnit() {
  unitBtn.textContent = `°${app.unit}`;
  unitBtn.setAttribute("aria-label", `Temperature unit: ${app.unit === "C" ? "Celsius" : "Fahrenheit"}. Switch to ${app.unit === "C" ? "Fahrenheit" : "Celsius"}`);
}
unitBtn.addEventListener("click", () => {
  app.unit = app.unit === "C" ? "F" : "C";
  saveUnit(app.unit);
  showUnit();
  renderAll();
  graphs.redraw();
});

$("diag-toggle").addEventListener("click", (e) => {
  const open = e.currentTarget.getAttribute("aria-expanded") !== "true";
  e.currentTarget.setAttribute("aria-expanded", String(open));
  $("diagnostics").hidden = !open;
});

const followBtn = $("follow-btn");
followBtn.addEventListener("click", () => {
  const on = followBtn.getAttribute("aria-pressed") !== "true";
  followBtn.setAttribute("aria-pressed", String(on));
  app.globe?.setFollow(on);
});
$("reset-btn").addEventListener("click", () => {
  followBtn.setAttribute("aria-pressed", "false");
  app.globe?.reset();
});

// ---------- Start ----------
addEventListener("error", (e) => noteError(e.message || "script error"));
addEventListener("unhandledrejection", (e) => noteError(String(e.reason?.message || e.reason)));
addEventListener("online", tick);
addEventListener("offline", tick);

initCards();
showUnit();
const graphs = createGraphs({ config: CONFIG, now, getUnit: () => app.unit, onError: noteError });

try {
  app.source = createSource(CONFIG);
} catch (err) {
  noteError(`Data source: ${err.message}`);
}

if (app.source) {
  const s = app.source;
  s.onClockOffset((ms) => { app.offsetMs = ms; });
  s.onConnection((c) => { app.connected = c; if (c) app.everConnected = true; tick(); });
  s.onLatest(onLatest, onReadError);
  s.onTouch(onTouch, onReadError);
  s.onMeta((m) => { app.meta = m; }, () => {});
  graphs.setSource(s);
} else {
  graphs.noSource("No history yet: the page is not connected to the station database.");
}

renderAll();
tick();
setInterval(tick, 1000);

// Close the live stream while the tab is hidden; refresh history if it was hidden for a while.
let hiddenAt = 0;
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    hiddenAt = Date.now();
    app.source?.pause();
    app.globe?.pause();
  } else {
    app.source?.resume();
    app.globe?.resume();
    if (Date.now() - hiddenAt > 60e3) graphs.reload();
  }
});

startSatellite({
  config: CONFIG,
  now,
  onError: noteError,
  onUpdate(sat) {
    app.sat = sat;
    app.rfLevel = renderRf(app.latest, rf, sat.available ? sat.pos.elDeg : null);
    app.globe?.update({ pos: sat.available ? sat.pos : null, tracker: sat.tracker, now: now(), rfLevel: app.rfLevel });
  },
});

// The globe is the heaviest part, so it loads after the first paint.
const globeMsg = $("globe-msg");
setTimeout(async () => {
  try {
    const { createGlobe } = await import("./ui/globe.js");
    app.globe = await createGlobe($("globe"), { station: CONFIG.station, satName: CONFIG.satellites[0].name });
    globeMsg.hidden = true;
  } catch (err) {
    globeMsg.textContent = `The 3D globe could not start (${err.message}). Satellite numbers below still work.`;
    noteError(`Globe: ${err.message}`);
  }
  if (app.sat && !app.sat.available) {
    globeMsg.hidden = false;
    globeMsg.textContent = "Orbit data unavailable: showing the station only, never a guessed satellite position.";
  }
}, 50);

document.documentElement.dataset.ready = "1";
