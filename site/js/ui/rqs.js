// The reception quality score instrument (SPEC 14.14): gauge, label and tag, four part bars, a 30 minute sparkline,
// next pass, and the "Why this score?" panel. Text goes in with textContent only.
import { rqsView, whyLines, sparkPoints, BANDS } from "../lib/rqs.js";
import { fmtHm, fmtCountdown, isNum } from "../lib/format.js";
import { setText } from "./cards.js";

const $ = (id) => document.getElementById(id);

// Gauge geometry: a 240 degree arc around (100, 100), 0 at the lower left, 100 at the lower right.
const CX = 100, CY = 100;
const angle = (v) => ((-120 + 2.4 * v) * Math.PI) / 180;
const pt = (v, r) => [CX + r * Math.sin(angle(v)), CY - r * Math.cos(angle(v))].map((n) => n.toFixed(2));
function arc(v0, v1, r) {
  if (!(v1 > v0)) return "";
  const [x0, y0] = pt(v0, r), [x1, y1] = pt(v1, r);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${(v1 - v0) * 2.4 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
}

const SPARK_W = 300, SPARK_H = 48;

export function initRqs() {
  // Thin band arcs outside the track, one per label, and the label lines as ticks.
  const ns = "http://www.w3.org/2000/svg";
  const bands = $("rqs-bands");
  for (const [id, b] of Object.entries(BANDS)) {
    const p = document.createElementNS(ns, "path");
    p.setAttribute("d", arc(b.from + 0.4, b.to - 0.4, 94));
    p.setAttribute("class", `rqs-band rqs-band-${id}`);
    bands.append(p);
  }
  $("rqs-track").setAttribute("d", arc(0, 100, 80));
  const ticks = $("rqs-ticks");
  for (const v of [15, 40, 70]) {
    const [x, y] = pt(v, 108);
    const t = document.createElementNS(ns, "text");
    t.setAttribute("x", x); t.setAttribute("y", (Number(y) + 3).toFixed(2));
    t.textContent = String(v);
    ticks.append(t);
  }
  const spark = $("rqs-spark-guides");
  for (const v of [15, 40, 70]) {
    const l = document.createElementNS(ns, "line");
    const y = (SPARK_H - 1 - (v / 100) * (SPARK_H - 2)).toFixed(1);
    l.setAttribute("x1", "0"); l.setAttribute("x2", String(SPARK_W)); l.setAttribute("y1", y); l.setAttribute("y2", y);
    spark.append(l);
  }

  const btn = $("rqs-why-btn"), panel = $("rqs-why");
  const show = (open) => { panel.hidden = !open; btn.setAttribute("aria-expanded", String(open)); };
  btn.addEventListener("click", () => show(panel.hidden));
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !panel.hidden) { show(false); btn.focus(); }
  });
}

function sparkPath(points, nowMs, windowMs) {
  const from = nowMs - windowMs;
  let d = "", pen = false;
  for (const p of points) {
    if (p.y === null) { pen = false; continue; }
    const x = (((p.x - from) / windowMs) * SPARK_W).toFixed(1);
    const y = (SPARK_H - 1 - (Math.min(100, Math.max(0, p.y)) / 100) * (SPARK_H - 2)).toFixed(1);
    d += `${pen ? "L" : "M"}${x} ${y} `;
    pen = true;
  }
  return d.trim();
}

export function renderRqs(rec, { stationOffline = false, meta = null, unit = "C", records = [], nowMs = Date.now() } = {}) {
  const v = rqsView(rec, { stationOffline });
  const panel = $("rqs-panel");
  panel.dataset.band = v.band;
  panel.dataset.state = v.state;
  $("rqs-value").setAttribute("d", v.value === null ? "" : arc(0, Math.max(0.6, v.value), 80));
  setText("rqs-num", v.text);
  setText("rqs-gauge-label", v.aria);
  setText("rqs-label", v.label);
  setText("rqs-tag", v.tag ?? "--");
  $("rqs-tag").dataset.tag = v.tag ?? "none";
  setText("rqs-tagwords", v.state === "warming" ? "learning the noise baseline" : v.tagWords);
  $("rqs-temp-na").hidden = v.state === "none" || !v.tempNa;
  $("rqs-batt-na").hidden = v.state !== "scored" || !v.battNa;
  $("rqs-detected").hidden = !v.detected;

  for (const b of v.bars) {
    const li = document.querySelector(`#rqs-bars [data-bar="${b.id}"]`);
    li.querySelector("i").style.setProperty("--v", b.value === null ? "0" : b.value.toFixed(3));
    li.dataset.empty = String(b.value === null);
    setText(`rqs-bar-${b.id}`, b.value === null ? "--" : b.value.toFixed(2));
  }

  const windowMs = 30 * 60e3;
  $("rqs-spark-line").setAttribute("d", sparkPath(sparkPoints(records, nowMs, windowMs), nowMs, windowMs));

  if (isNum(v.nextPassMs) && v.nextPassMs > nowMs) {
    setText("rqs-next", `${fmtHm(new Date(v.nextPassMs))}, in ${fmtCountdown(v.nextPassMs - nowMs)}`);
  } else {
    setText("rqs-next", "--");
  }
  setText("rqs-el", isNum(v.passElDeg) ? `${Math.round(v.passElDeg)}°` : v.state === "none" ? "--" : "not in a pass");
  setText("rqs-corr", isNum(v.passCorr) ? v.passCorr.toFixed(2) : "--");

  const rows = $("rqs-why-rows");
  const items = whyLines(rec, meta, unit).map(([title, text]) => {
    const div = document.createElement("div");
    const dt = document.createElement("dt"); dt.textContent = title;
    const dd = document.createElement("dd"); dd.textContent = text;
    div.append(dt, dd);
    return div;
  });
  rows.replaceChildren(...items);
}
