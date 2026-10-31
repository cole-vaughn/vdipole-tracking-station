// Graphs section: history query per range, live points appended at the right edge, gaps for null.
// Charts are only built for the visible tab, so the page stays light.
import { fmtClock, toUnit, isNum } from "../lib/format.js";
import { downsample, insertGaps, pickSeries } from "../lib/series.js";

const C = { accent: "#4fd1ff", amber: "#ffb454", good: "#5ee09a", raw: "#7d8fa3", line: "#1f2b3a", muted: "#7d8fa3" };
const raw = (field, label = "Raw") => ({ field, label, color: C.raw, width: 1 });

const GROUPS = [
  { id: "env", label: "Environment", charts: [
    { id: "bmp-temp", title: "BMP388 temperature", unit: "temp", series: [{ field: "tempC", label: "BMP388", color: C.amber }] },
    { id: "pressure", title: "Pressure", unit: "hPa", digits: 1, series: [{ field: "pressHPa", label: "Pressure", color: C.accent }] },
    { id: "altitude", title: "Altitude from pressure", unit: "m", note: "Drifts with the weather, not true altitude", series: [{ field: "altM", label: "Altitude", color: C.accent }] },
    { id: "therm", title: "Thermistor raw vs filtered", unit: "temp", note: "Unverified calibration", series: [raw("thermRaw"), { field: "thermFilt", label: "Filtered", color: C.accent }] },
    { id: "bmp-therm", title: "BMP388 vs thermistor", unit: "temp", note: "Thermistor unverified", series: [{ field: "tempC", label: "BMP388", color: C.amber }, { field: "thermFilt", label: "Thermistor", color: C.accent }] },
  ] },
  { id: "radio", label: "Radio", charts: [
    { id: "rf", title: "RF power raw vs filtered", unit: "dBm", digits: 1, note: "Broadband, not a spectrum", series: [raw("rfRawDbm"), { field: "rfFiltDbm", label: "Filtered", color: C.accent }] },
  ] },
  { id: "power", label: "Power", charts: [
    { id: "batt-v", title: "Battery voltage", unit: "V", digits: 2, series: [{ field: "battV", label: "Voltage", color: C.good }] },
    { id: "batt-ma", title: "Load current", unit: "mA", series: [{ field: "battmA", label: "Current", color: C.accent }] },
    { id: "batt-mw", title: "Load power", unit: "mW", series: [{ field: "battmW", label: "Power", color: C.accent }] },
    { id: "batt-soc", title: "Battery charge", unit: "%", digits: 1, note: "Estimate", series: [{ field: "battSocPct", label: "Charge", color: C.good }] },
  ] },
  { id: "misc", label: "Fan, touch, WiFi", charts: [
    { id: "fan", title: "Fan speed", unit: "RPM", series: [{ field: "fanRpm", label: "Fan", color: C.accent }] },
    { id: "cap-delta", title: "Touch delay change", unit: "µs", digits: 1, series: [{ field: "capDeltaUs", label: "Delta", color: C.accent }] },
    { id: "cap-td", title: "Touch charge delay", unit: "µs", digits: 1, series: [{ field: "capTdUs", label: "Delay", color: C.accent }] },
    { id: "cap-pf", title: "Touch capacitance", unit: "pF", digits: 1, note: "Uncalibrated", series: [{ field: "capPF", label: "Capacitance", color: C.raw }] },
    { id: "rssi", title: "WiFi signal", unit: "dBm", series: [{ field: "rssi", label: "RSSI", color: C.accent }] },
  ] },
];

// Hover: for each series, the reading nearest the cursor's time (binary search), so the tooltip shows every
// series at one moment instead of hopping between lines. Nothing is shown inside a gap.
function registerHover(Chart) {
  if (Chart.Interaction.modes.timeEach) return;
  Chart.Interaction.modes.timeEach = (chart, e) => {
    const px = e.x ?? Chart.helpers.getRelativePosition(e, chart).x;
    const { left, right } = chart.chartArea;
    if (px < left || px > right) return [];
    const xv = chart.scales.x.getValueForPixel(px);
    const items = [];
    chart.data.datasets.forEach((ds, di) => {
      const data = ds.data;
      const meta = chart.getDatasetMeta(di);
      if (meta.hidden || !data.length) return;
      let lo = 0, hi = data.length - 1;
      while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (data[mid].x < xv) lo = mid; else hi = mid; }
      const i = Math.abs(data[lo].x - xv) <= Math.abs(data[hi].x - xv) ? lo : hi;
      const el = meta.data[i];
      if (!el || data[i].y === null || Math.abs(el.x - px) > 30) return;
      items.push({ element: el, datasetIndex: di, index: i });
    });
    return items;
  };
}

// Vertical line that follows the (animated) tooltip, so the hover moves smoothly.
const crosshair = {
  id: "crosshair",
  afterDatasetsDraw(chart) {
    const tip = chart.tooltip;
    if (!tip || !tip.opacity || !tip.getActiveElements().length) return;
    const { ctx, chartArea: { top, bottom } } = chart;
    ctx.save();
    ctx.globalAlpha = tip.opacity;
    ctx.strokeStyle = "rgba(125, 143, 163, 0.55)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(Math.round(tip.caretX) + 0.5, top);
    ctx.lineTo(Math.round(tip.caretX) + 0.5, bottom);
    ctx.stroke();
    ctx.restore();
  },
};

const pad = (n) => String(n).padStart(2, "0");
const tick = (v) => { const d = new Date(v); return `${pad(d.getHours())}:${pad(d.getMinutes())}`; };

export function createGraphs({ config, now, getUnit, onError }) {
  const g = config.graphs;
  const msg = document.getElementById("graphs-msg");
  let source = null;
  let range = g.ranges.find((r) => r.id === g.defaultRange);
  let activeGroup = GROUPS[0].id;
  let records = [];
  let loadedSince = Infinity;
  let retryMs = 2000, retryTimer = null, redrawTimer = null, loading = false;
  const charts = new Map(); // chart id -> Chart instance

  // ----- Controls -----
  const rangeGroup = document.getElementById("range-group");
  for (const r of g.ranges) {
    const b = document.createElement("button");
    b.type = "button"; b.className = "btn"; b.textContent = r.label; b.dataset.range = r.id;
    b.setAttribute("aria-pressed", String(r.id === range.id));
    b.addEventListener("click", () => setRange(r.id));
    rangeGroup.append(b);
  }
  const tabs = document.getElementById("graph-tabs");
  const panels = document.getElementById("graph-panels");
  for (const grp of GROUPS) {
    const tab = document.createElement("button");
    tab.type = "button"; tab.className = "tab"; tab.role = "tab"; tab.id = `tab-${grp.id}`;
    tab.textContent = grp.label;
    tab.setAttribute("aria-controls", `panel-${grp.id}`);
    tab.setAttribute("aria-selected", String(grp.id === activeGroup));
    tab.tabIndex = grp.id === activeGroup ? 0 : -1;
    tab.addEventListener("click", () => setGroup(grp.id));
    tab.addEventListener("keydown", (e) => {
      if (e.key !== "ArrowRight" && e.key !== "ArrowLeft") return;
      const i = GROUPS.findIndex((x) => x.id === activeGroup);
      const next = GROUPS[(i + (e.key === "ArrowRight" ? 1 : GROUPS.length - 1)) % GROUPS.length];
      setGroup(next.id);
      document.getElementById(`tab-${next.id}`).focus();
    });
    tabs.append(tab);

    const panel = document.createElement("div");
    panel.className = "chart-grid"; panel.role = "tabpanel"; panel.id = `panel-${grp.id}`;
    panel.setAttribute("aria-labelledby", tab.id);
    panel.hidden = grp.id !== activeGroup;
    for (const c of grp.charts) {
      panel.insertAdjacentHTML("beforeend",
        `<figure class="chart-card" data-chart="${c.id}"><figcaption><span>${c.title}</span>` +
        `<span class="note" data-note>${c.note ?? ""}</span></figcaption>` +
        `<div class="chart-box"><canvas aria-label="${c.title} chart" role="img"></canvas></div></figure>`);
    }
    panels.append(panel);
  }

  function setGroup(id) {
    activeGroup = id;
    for (const grp of GROUPS) {
      const on = grp.id === id;
      const tab = document.getElementById(`tab-${grp.id}`);
      tab.setAttribute("aria-selected", String(on));
      tab.tabIndex = on ? 0 : -1;
      document.getElementById(`panel-${grp.id}`).hidden = !on;
    }
    redraw();
  }

  function setRange(id) {
    range = g.ranges.find((r) => r.id === id);
    rangeGroup.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.range === id)));
    if (now() - range.ms < loadedSince) load(); else redraw();
  }

  // ----- Data -----
  async function load() {
    if (!source || loading) return;
    clearTimeout(retryTimer);
    loading = true;
    const since = now() - range.ms;
    const limit = Math.min(9000, Math.ceil(range.ms / (g.historyIntervalS * 1000)) + 60);
    msg.textContent = "Loading history";
    try {
      const fetched = (await source.fetchHistory(since, limit)).filter((r) => r && isNum(r.ts)).sort((a, b) => a.ts - b.ts);
      const lastTs = fetched.length ? fetched[fetched.length - 1].ts : -Infinity;
      records = fetched.concat(records.filter((r) => r.ts > lastTs));
      loadedSince = since;
      retryMs = 2000;
      msg.textContent = records.length ? "" : "No readings in this range yet.";
    } catch (err) {
      msg.textContent = `Could not load history (${err.message}). Retrying in ${Math.round(retryMs / 1000)} s.`;
      onError?.(`History: ${err.message}`);
      retryTimer = setTimeout(load, retryMs);
      retryMs = Math.min(60000, retryMs * 2);
    } finally {
      loading = false;
    }
    redraw();
  }

  function addLive(rec) {
    if (!rec || !isNum(rec.ts)) return;
    const last = records[records.length - 1];
    if (last && rec.ts <= last.ts) return;
    records.push(rec);
    const keepFrom = now() - 24 * 3600e3 - 600e3;
    while (records.length && records[0].ts < keepFrom) records.shift();
    // Redraw at most every 5 s for live points.
    if (!redrawTimer) redrawTimer = setTimeout(() => { redrawTimer = null; redraw(); }, 5000);
  }

  // ----- Drawing -----
  // About one point per pixel of chart width (never more than the spec's 1,500): smooth lines, calm hover.
  function datasetPoints(series, unit, from, widthPx) {
    const inRange = records.filter((r) => r.ts >= from);
    const transform = series.unit === "temp" ? (v) => toUnit(v, unit) : (v) => v;
    const max = Math.min(g.maxPoints, Math.max(200, Math.round(widthPx)));
    return downsample(insertGaps(pickSeries(inRange, series.field, transform), g.gapMs), max);
  }

  function redraw() {
    const Chart = window.Chart;
    if (!Chart) { msg.textContent = "The charts library did not load. Check the connection and reload."; return; }
    Chart.defaults.font.family = '"IBM Plex Mono", ui-monospace, monospace';
    Chart.defaults.font.size = 11;
    registerHover(Chart);
    const unit = getUnit();
    const t1 = now(), t0 = t1 - range.ms;
    const grp = GROUPS.find((x) => x.id === activeGroup);
    for (const c of grp.charts) {
      const card = document.querySelector(`[data-chart="${c.id}"]`);
      const unitLabel = c.unit === "temp" ? `°${unit}` : c.unit;
      const width = card.querySelector(".chart-box").clientWidth || 600;
      const data = c.series.map((s) => datasetPoints({ ...s, unit: c.unit }, unit, t0, width));
      const empty = data.every((d) => d.every((p) => p.y === null));
      card.querySelector("[data-note]").textContent = empty ? "No readings in this range" : c.note ?? "";

      let chart = charts.get(c.id);
      if (!chart) {
        chart = new Chart(card.querySelector("canvas"), chartConfig(c));
        charts.set(c.id, chart);
      }
      chart.data.datasets.forEach((ds, i) => { ds.data = data[i]; });
      chart.options.scales.x.min = t0;
      chart.options.scales.x.max = t1;
      chart.options.scales.y.title.text = unitLabel;
      chart.$unit = unitLabel;
      chart.update("none");
    }
  }

  function chartConfig(c) {
    const digits = c.digits ?? 0;
    return {
      type: "line",
      data: {
        datasets: c.series.map((s) => ({
          label: s.label, data: [], borderColor: s.color, backgroundColor: s.color,
          borderWidth: s.width ?? 1.6, pointRadius: 0, pointHoverRadius: 3.5, pointHoverBorderWidth: 0,
          spanGaps: false, tension: 0,
        })),
      },
      plugins: [crosshair],
      options: {
        // Data redraws are instant; only the tooltip glides between readings.
        animation: { duration: 0 }, parsing: false, normalized: true, responsive: true, maintainAspectRatio: false,
        interaction: { mode: "timeEach", intersect: false },
        scales: {
          x: { type: "linear", ticks: { callback: tick, maxTicksLimit: 6, color: C.muted }, grid: { color: C.line } },
          y: { title: { display: true, text: "", color: C.muted }, ticks: { color: C.muted }, grid: { color: C.line } },
        },
        plugins: {
          legend: { display: c.series.length > 1, labels: { color: C.muted, boxWidth: 12, boxHeight: 2 } },
          tooltip: {
            animation: { duration: 140, easing: "easeOutQuad" },
            position: "average",
            caretPadding: 10,
            callbacks: {
              title: (items) => `${fmtClock(new Date(items[0].parsed.x))} local, ${fmtClock(new Date(items[0].parsed.x), true)} UTC`,
              label: (item) => `${item.dataset.label}: ${item.parsed.y.toFixed(digits)} ${item.chart.$unit}`,
            },
          },
        },
      },
    };
  }

  return {
    setSource(s) { source = s; load(); },
    noSource(text) { msg.textContent = text; redraw(); },
    addLive,
    redraw,
    reload() { loadedSince = Infinity; load(); },
    get records() { return records; },
  };
}
