// Battery health pill and its detail panel (SPEC 14.14). A click or tap opens it; hover also opens it on a
// desktop. Escape or a click outside closes it.
import { pillView, panelRows } from "../lib/health.js";
import { setText } from "./cards.js";

const $ = (id) => document.getElementById(id);
const canHover = matchMedia("(hover: hover) and (pointer: fine)");
let pinned = false, hoverTimer = null, lastRows = "";

function setOpen(open) {
  $("soh-panel").hidden = !open;
  $("soh-pill").setAttribute("aria-expanded", String(open));
  if (!open) pinned = false;
}

export function initHealth() {
  const pill = $("soh-pill"), panel = $("soh-panel");
  pill.addEventListener("click", () => {
    if (pinned) { setOpen(false); return; }
    pinned = true;
    setOpen(true);
  });
  const enter = () => {
    if (!canHover.matches) return;
    clearTimeout(hoverTimer);
    setOpen(true);
  };
  const leave = () => {
    if (!canHover.matches || pinned) return;
    clearTimeout(hoverTimer);
    hoverTimer = setTimeout(() => setOpen(false), 200);
  };
  for (const el of [pill, panel]) {
    el.addEventListener("mouseenter", enter);
    el.addEventListener("mouseleave", leave);
  }
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape" || panel.hidden) return;
    const focusInside = panel.contains(document.activeElement);
    setOpen(false);
    if (focusInside) pill.focus();
  });
  document.addEventListener("pointerdown", (e) => {
    if (!panel.hidden && !panel.contains(e.target) && !pill.contains(e.target)) setOpen(false);
  });
}

export function renderHealth(h, records, { stationOffline }, cfg) {
  const v = pillView(h, { stationOffline }, cfg);
  const pill = $("soh-pill");
  pill.dataset.band = v.band;
  pill.dataset.state = v.state;
  pill.dataset.conf = v.conf;
  pill.setAttribute("aria-label", v.aria);
  setText("soh-text", v.text);
  $("soh-est").hidden = !v.est;
  $("soh-uncal").hidden = !(h && h.inaCal === false);

  // Values can come from the database, so they go in as text, never as HTML.
  const rows = panelRows(h, records, cfg);
  const key = JSON.stringify(rows);
  if (key === lastRows) return;
  lastRows = key;
  const dl = $("soh-rows");
  dl.replaceChildren(...rows.map(([label, value]) => {
    const div = document.createElement("div");
    const dt = document.createElement("dt");
    const dd = document.createElement("dd");
    dt.textContent = label;
    dd.textContent = value;
    div.append(dt, dd);
    return div;
  }));
}
