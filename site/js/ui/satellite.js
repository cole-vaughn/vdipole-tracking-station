// Loads the orbital elements, runs SGP4 once a second, and fills the satellite readouts.
import { createTracker } from "../lib/orbit.js";
import { fmtAge, fmtClock, fmtCountdown, fmtInt, fmtHm, dayLabel } from "../lib/format.js";
import { setText } from "./cards.js";

const SATLIB_URL = new URL("../../vendor/satellite.lib.js", import.meta.url).href; // self-hosted, see tools/vendor.mjs

export async function startSatellite({ config, now, onUpdate, onError }) {
  const target = config.satellites[0];
  setText("sat-name", target.name);
  setText("sat-downlink", target.downlink);

  let tracker;
  try {
    const res = await fetch(config.orbit.file, { cache: "no-cache" });
    if (!res.ok) throw new Error(`orbit file HTTP ${res.status}`);
    const doc = await res.json();
    const sat = doc.satellites?.find((s) => s.noradId === target.noradId);
    if (!sat) throw new Error(`no orbital elements for NORAD ${target.noradId}`);
    const satlib = await import(SATLIB_URL);
    tracker = createTracker(sat, config.station, satlib);
  } catch (err) {
    unavailable();
    onError?.(`Orbit data: ${err.message}`);
    onUpdate({ available: false });
    return null;
  }

  const staleMs = config.orbit.staleDays * 86400e3;
  const o = config.orbit;
  let passes = [];
  let passesAt = 0;

  function tick() {
    const t = new Date(now());
    const pos = tracker.positionAt(t);
    if (!pos) { unavailable(); onUpdate({ available: false }); return; }

    // Recompute when a pass ends, and every 10 minutes in any case.
    if (!passes.length || t > passes[0].los || t - passesAt > 600e3) {
      passes = tracker.nextPasses(t, o.passListCount, o.passListHours, o.minPassElevationDeg);
      passesAt = t;
      renderPasses(passes, t, o.lowPassDeg);
    } else if (t >= passes[0].aos && !document.querySelector("#pass-rows tr.now")) {
      renderPasses(passes, t, o.lowPassDeg); // a pass just started: mark it "Now"
    }
    const pass = passes[0] && passes[0].aos - t < o.passSearchHours * 3600e3 ? passes[0] : null;
    const up = pos.elDeg > 0;
    const vis = document.getElementById("sat-vis");
    vis.dataset.up = String(up);
    setText("sat-vis", up ? "Above horizon" : "Below horizon");
    setText("sat-az", `${pos.azDeg.toFixed(0)}°`);
    setText("sat-el", `${pos.elDeg.toFixed(1)}°`);
    setText("sat-range", `${fmtInt(pos.rangeKm)} km`);

    const next = document.getElementById("sat-next");
    if (!pass) {
      setText("sat-next", "none in 24 h");
    } else if (t >= pass.aos) {
      setText("sat-next", `now, ends in ${fmtCountdown(pass.los - t)}`);
    } else {
      setText("sat-next", `in ${fmtCountdown(pass.aos - t)}, max ${pass.maxElDeg.toFixed(0)}°`);
      next.title = `Starts ${fmtClock(pass.aos)} local, ${fmtClock(pass.aos, true)} UTC`;
    }

    const ageMs = t - tracker.epoch;
    const old = ageMs > staleMs;
    setText("sat-tle", `${fmtAge(ageMs)} old${old ? " (stale: positions less accurate)" : ""}`);
    document.getElementById("sat-tle").classList.toggle("warn", old);

    onUpdate({ available: true, pos, pass, epoch: tracker.epoch, tleAgeMs: ageMs, tracker });
  }

  tick();
  setInterval(tick, 1000);
  return tracker;
}

function unavailable() {
  setText("sat-vis", "Orbit data unavailable");
  for (const id of ["sat-az", "sat-el", "sat-range", "sat-next", "sat-tle"]) setText(id, "--");
  document.getElementById("pass-rows").innerHTML = `<tr><td colspan="5">Orbit data unavailable</td></tr>`;
}

function renderPasses(passes, now, lowDeg) {
  const rows = passes.map((p) => {
    const live = now >= p.aos;
    const low = p.maxElDeg < lowDeg;
    const utc = `UTC ${fmtHm(p.aos, true)} to ${fmtHm(p.los, true)}`;
    return `<tr class="${live ? "now" : ""}" title="${utc}">` +
      `<td>${live ? "Now" : dayLabel(p.aos, now)}</td><td>${fmtHm(p.aos)}</td><td>${fmtHm(p.los)}</td>` +
      `<td>${Math.round((p.los - p.aos) / 60e3)} min</td>` +
      `<td>${p.maxElDeg.toFixed(0)}°${low ? ' <span class="tag">low</span>' : ""}</td></tr>`;
  });
  document.getElementById("pass-rows").innerHTML = rows.length ? rows.join("") : `<tr><td colspan="5">No passes in the next 48 h</td></tr>`;
}
