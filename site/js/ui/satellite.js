// Loads the orbital elements, runs SGP4 once a second, and fills the satellite readouts.
import { createTracker } from "../lib/orbit.js";
import { fmtAge, fmtClock, fmtCountdown, fmtInt } from "../lib/format.js";
import { setText } from "./cards.js";

const SATLIB_URL = "https://cdn.jsdelivr.net/npm/satellite.js@7.1.0/+esm";

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
  let pass = null;

  function tick() {
    const t = new Date(now());
    const pos = tracker.positionAt(t);
    if (!pos) { unavailable(); onUpdate({ available: false }); return; }

    if (!pass || t > pass.los) {
      pass = tracker.nextPass(t, config.orbit.passSearchHours, config.orbit.minPassElevationDeg);
    }
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
}
