// SGP4 wrapper around satellite.js. The library is passed in, so the browser can use the CDN build
// and the unit tests can use the npm copy of the same version.

const DEG = 180 / Math.PI;
const EARTH_R_KM = 6371;

export function jdToDate(jd) {
  return new Date((jd - 2440587.5) * 86400000);
}

export function createTracker(sat, station, satlib) {
  const satrec = satlib.twoline2satrec(sat.tle1, sat.tle2);
  if (satrec.error) throw new Error(`Bad orbital elements for ${sat.name} (SGP4 error ${satrec.error})`);
  const epoch = jdToDate(satrec.jdsatepoch);
  const observer = {
    latitude: station.lat / DEG,
    longitude: station.lon / DEG,
    height: station.altKm,
  };

  function eciAt(date) {
    const pv = satlib.propagate(satrec, date);
    return pv && pv.position && typeof pv.position === "object" ? pv.position : null;
  }

  function positionAt(date) {
    const eci = eciAt(date);
    if (!eci) return null;
    const gmst = satlib.gstime(date);
    const gd = satlib.eciToGeodetic(eci, gmst);
    const look = satlib.ecfToLookAngles(observer, satlib.eciToEcf(eci, gmst));
    return {
      lat: satlib.degreesLat(gd.latitude),
      lon: satlib.degreesLong(gd.longitude),
      altKm: gd.height,
      azDeg: (look.azimuth * DEG + 360) % 360,
      elDeg: look.elevation * DEG,
      rangeKm: look.rangeSat,
    };
  }

  // Sub-satellite points from minutesBack to minutesAhead, for the ground track.
  function track(date, minutesBack, minutesAhead, stepS = 30) {
    const out = [];
    for (let s = -minutesBack * 60; s <= minutesAhead * 60; s += stepS) {
      const p = positionAt(new Date(date.getTime() + s * 1000));
      if (p) out.push({ t: s, lat: p.lat, lon: p.lon, altKm: p.altKm });
    }
    return out;
  }

  // Finds the elevation crossing between two times by bisection (to about one second).
  function crossing(t0, t1, minEl) {
    const el = (t) => positionAt(new Date(t))?.elDeg ?? -90;
    let a = t0, b = t1;
    const aboveA = el(a) > minEl;
    while (b - a > 1000) {
      const m = (a + b) / 2;
      if ((el(m) > minEl) === aboveA) a = m; else b = m;
    }
    return new Date(b);
  }

  // The current pass (if the satellite is up) or the next one within searchHours.
  function nextPass(from, searchHours = 24, minEl = 0, stepS = 30) {
    const start = from.getTime();
    const end = start + searchHours * 3600e3;
    const step = stepS * 1000;
    const now = positionAt(from);
    if (!now) return null;
    let inPass = now.elDeg > minEl;
    let aos = inPass ? from : null;
    let maxEl = inPass ? now.elDeg : -90;
    let maxElTime = inPass ? from : null;
    // Search runs an extra hour past the end so a pass that starts near the end can finish.
    for (let t = start + step; t <= end + 3600e3; t += step) {
      const p = positionAt(new Date(t));
      if (!p) return null;
      const up = p.elDeg > minEl;
      if (!aos && up) aos = crossing(t - step, t, minEl);
      if (aos && up && p.elDeg > maxEl) { maxEl = p.elDeg; maxElTime = new Date(t); }
      if (aos && !up) {
        return { aos, los: crossing(t - step, t, minEl), maxElDeg: maxEl, maxElTime, inProgress: inPass };
      }
      if (!aos && t > end) break;
    }
    return null;
  }

  return { satrec, epoch, eciAt, positionAt, track, nextPass };
}

// Radius on the ground of the area that can see the satellite (elevation above 0).
export function footprintRadiusKm(altKm) {
  return EARTH_R_KM * Math.acos(EARTH_R_KM / (EARTH_R_KM + altKm));
}
