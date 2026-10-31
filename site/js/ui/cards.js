// Instrument cards: battery, RF meter, fan, touch, environment. Each one renders "--" for null.
import { fmt, fmtInt, fmtHoursLeft, fmtTemp, fmtClock, isNum } from "../lib/format.js";
import { batteryBand, isLowBattery } from "../lib/battery.js";

const $ = (id) => document.getElementById(id);
export function setText(id, text) {
  const el = $(id);
  if (el && el.textContent !== text) el.textContent = text;
}

const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
let fanAnim = null;

export function initCards() {
  // S-meter scale: 0 to 12+ dB above baseline across the -70 to +70 degree sweep.
  const ticks = $("smeter-ticks");
  const labels = ["0", "3", "6", "9", "12+"];
  let svg = "";
  labels.forEach((label, i) => {
    const a = ((-70 + (140 * i) / 4) * Math.PI) / 180;
    const p = (r) => [110 + r * Math.sin(a), 105 - r * Math.cos(a)].map((n) => n.toFixed(1));
    const [x1, y1] = p(70), [x2, y2] = p(86), [tx, ty] = p(98);
    svg += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}"/><text x="${tx}" y="${ty}">${label}</text>`;
  });
  svg += `<text x="110" y="70">dB</text>`;
  ticks.innerHTML = svg;

  const blades = $("fan-blades");
  if (blades.animate) {
    fanAnim = blades.animate([{ transform: "rotate(0deg)" }, { transform: "rotate(360deg)" }], { duration: 1000, iterations: Infinity });
    fanAnim.pause();
  }
}

export function renderBattery(rec, cfg, { stationOffline, charge, chargingCfg }) {
  const pct = rec?.battSocPct;
  const v = rec?.battV;
  const band = stationOffline ? "none" : batteryBand(pct, v, cfg);
  const batt = $("batt");
  batt.dataset.band = band;
  $("batt-fill").style.setProperty("--pct", isNum(pct) ? Math.min(100, Math.max(0, pct)).toFixed(1) : "0");
  batt.setAttribute("aria-label", isNum(pct) ? `Battery about ${Math.round(pct)} percent (estimate)` : "Battery charge unknown");
  setText("batt-pct", isNum(pct) ? `~${Math.round(pct)}` : "--");
  setText("batt-left", fmtHoursLeft(rec?.battHoursLeft));
  setText("batt-v", fmt(v, 2));
  setText("batt-ma", fmt(rec?.battmA, 0));
  setText("batt-mw", fmtInt(rec?.battmW));
  setText("batt-wh", fmt(rec?.battWhLeft, 1));
  setText("batt-peak", fmt(rec?.battPeakmA, 0));

  const warn = $("batt-warn");
  if (isLowBattery(pct, v, cfg)) {
    warn.hidden = false;
    warn.textContent = isNum(v) && v < cfg.lowVolts
      ? `Low voltage: under ${cfg.lowVolts} V (the pack cuts off at 9.0 V)`
      : `Low battery: under ${cfg.redBelowPct}%`;
  } else {
    warn.hidden = true;
  }
  $("batt-charge").hidden = !(chargingCfg.showBadge && charge?.state === "charging" && !stationOffline);
}

export function renderRf(rec, rf, satElDeg) {
  const dbm = rec?.rfFiltDbm;
  const base = rf.baseline();
  const level = rf.level(dbm);
  const excess = rf.excessDb(dbm);
  const panel = document.querySelector(".rf-panel");
  panel.dataset.level = base === null || !isNum(dbm) ? "none" : "ok";
  $("smeter-needle").style.transform = `rotate(${(-70 + 140 * level).toFixed(1)}deg)`;
  setText("rf-excess", excess === null ? "--" : (excess >= 0 ? "+" : "") + excess.toFixed(1));
  setText("rf-dbm", fmt(dbm, 1));
  setText("rf-base", base === null ? (isNum(dbm) ? "learning" : "--") : base.toFixed(1));
  setText("rf-el", isNum(satElDeg) ? `${satElDeg.toFixed(0)}° ${satElDeg > 0 ? "(up)" : "(below horizon)"}` : "--");
  return level;
}

export function renderFan(rec, cfg) {
  const rpm = rec?.fanRpm;
  const panel = document.querySelector(".fan-panel");
  let state, label;
  if (!isNum(rpm)) { state = "none"; label = "Not connected"; }
  else if (rec.fanStalled || rpm < cfg.stallRpm) { state = "stalled"; label = "Stalled"; }
  else { state = "spinning"; label = "Spinning"; }
  panel.dataset.state = state;
  setText("fan-state", label);
  setText("fan-rpm", fmtInt(rpm));

  // Visual speed follows RPM but is capped, so the blades never strobe (wagon-wheel effect).
  if (!fanAnim) return;
  const rps = state === "spinning" && !reducedMotion.matches
    ? cfg.maxVisualRps * Math.min(1, rpm / cfg.rpmAtMaxVisual) : 0;
  if (rps > 0) {
    fanAnim.updatePlaybackRate(rps);
    if (fanAnim.playState !== "running") fanAnim.play();
  } else {
    fanAnim.pause();
  }
}

// The touch node is written the moment a press changes, so it is fresher than /latest for state.
export function touchView(latest, touch) {
  const touchNewer = touch && isNum(touch.ts) && (!latest || !isNum(latest.ts) || touch.ts >= latest.ts);
  return {
    fault: latest?.capFault === true,
    touched: touch ? touch.state === true : latest ? latest.touched === true : null,
    count: touchNewer ? touch.count : latest?.touchCount ?? touch?.count,
    lastTs: touch?.ts,
    deltaUs: latest?.capDeltaUs,
  };
}

export function renderTouch(view) {
  const el = $("touch-state");
  let state = "unknown", label = "--";
  if (view.fault) { state = "fault"; label = "Sensor fault"; }
  else if (view.touched === true) { state = "touched"; label = "Touched"; }
  else if (view.touched === false) { state = "idle"; label = "Idle"; }
  el.dataset.state = state;
  setText("touch-label", label);
  setText("touch-count", fmtInt(view.count));
  setText("touch-last", isNum(view.lastTs) ? fmtClock(new Date(view.lastTs)) : "--");
  setText("touch-delta", fmt(view.deltaUs, 1));
}

export function flashTouch() {
  const panel = $("touch-panel");
  panel.classList.remove("flash");
  void panel.offsetWidth; // restart the animation
  panel.classList.add("flash");
}

export function renderEnv(rec, unit) {
  setText("env-bmp", fmtTemp(rec?.tempC, unit));
  setText("env-therm", fmtTemp(rec?.thermFilt, unit));
  setText("env-therm-raw", isNum(rec?.thermRaw) ? `${fmtTemp(rec.thermRaw, unit)} °${unit}` : "--");
  document.querySelectorAll(".temp-unit").forEach((el) => { el.textContent = `°${unit}`; });
}
