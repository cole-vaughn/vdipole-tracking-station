// 3D globe: three.js through globe.gl (pinned CDN builds, mapped in index.html). Free textures, no key or account:
// NASA Blue Marble by day and NASA Earth at Night city lights, blended along the real sunrise line.
import * as THREE from "three";
import Globe from "globe.gl";
import { buildSatellite } from "./satellite-model.js";

const IMG = "https://cdn.jsdelivr.net/npm/three-globe@2.45.6/example/img/";
const R_KM = 6371;
const RAD = Math.PI / 180;

// Point on Earth where the Sun is overhead (accurate to about 0.1 degree).
export function subsolar(date) {
  const n = date.getTime() / 86400e3 + 2440587.5 - 2451545.0;
  const L = (280.46 + 0.9856474 * n) % 360;
  const g = ((357.528 + 0.9856003 * n) % 360) * RAD;
  const lambda = (L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g)) * RAD;
  const eps = (23.439 - 0.0000004 * n) * RAD;
  const dec = Math.asin(Math.sin(eps) * Math.sin(lambda)) / RAD;
  const ra = Math.atan2(Math.cos(eps) * Math.sin(lambda), Math.cos(lambda)) / RAD;
  const gmst = (280.46061837 + 360.98564736629 * n) % 360;
  return { lat: dec, lon: ((((ra - gmst) % 360) + 540) % 360) - 180 };
}

// Day and night textures blended by the angle to the Sun (adapted from the globe.gl day-night example).
const vertexShader = `
  varying vec3 vNormal; varying vec2 vUv;
  void main() { vNormal = normalize(normalMatrix * normal); vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`;
const fragmentShader = `
  #define PI 3.141592653589793
  uniform sampler2D dayTexture; uniform sampler2D nightTexture;
  uniform vec2 sunPosition; uniform vec2 globeRotation;
  varying vec3 vNormal; varying vec2 vUv;
  float toRad(float a) { return a * PI / 180.0; }
  vec3 polarToCartesian(vec2 c) {
    float theta = toRad(90.0 - c.x); float phi = toRad(90.0 - c.y);
    return vec3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
  }
  void main() {
    float invLon = toRad(globeRotation.x); float invLat = -toRad(globeRotation.y);
    mat3 rotX = mat3(1, 0, 0, 0, cos(invLat), -sin(invLat), 0, sin(invLat), cos(invLat));
    mat3 rotY = mat3(cos(invLon), 0, sin(invLon), 0, 1, 0, -sin(invLon), 0, cos(invLon));
    float intensity = dot(normalize(vNormal), normalize(rotX * rotY * polarToCartesian(sunPosition)));
    vec4 day = texture2D(dayTexture, vUv); vec4 night = texture2D(nightTexture, vUv);
    gl_FragColor = mix(night * 1.25, day, smoothstep(-0.12, 0.12, intensity));
  }`;

export async function createGlobe(container, { station, satName }) {
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const small = matchMedia("(max-width: 699px)").matches;

  const loader = new THREE.TextureLoader();
  const [day, night] = await Promise.all([
    loader.loadAsync(IMG + "earth-blue-marble.jpg"),
    loader.loadAsync(IMG + "earth-night.jpg"),
  ]);
  const material = new THREE.ShaderMaterial({
    uniforms: {
      dayTexture: { value: day }, nightTexture: { value: night },
      sunPosition: { value: new THREE.Vector2() }, globeRotation: { value: new THREE.Vector2() },
    },
    vertexShader, fragmentShader,
  });

  const globe = new Globe(container, { animateIn: false });
  globe.renderer().setPixelRatio(Math.min(small ? 1.5 : 2, devicePixelRatio));
  const fit = () => globe.width(container.clientWidth).height(container.clientHeight);
  fit();
  new ResizeObserver(fit).observe(container);
  globe.globeMaterial(material).backgroundImageUrl(IMG + "night-sky.png")
    .atmosphereColor("#8fdcff").atmosphereAltitude(0.16)
    .onZoom(({ lng, lat }) => material.uniforms.globeRotation.value.set(lng, lat));

  // Satellite: illustrative model (not an exact replica), drawn about 500 km across so it can be seen.
  // globe.gl turns the outer group to face Earth; the inner tilt lets a top-down camera see the wings.
  const inner = buildSatellite(THREE);
  inner.rotation.x = -0.75;
  inner.rotation.y = 0.35;
  const model = new THREE.Group();
  model.add(inner);
  model.scale.setScalar(0.8);
  const satObj = { lat: 0, lng: 0, alt: 0.13 };
  globe.objectsData([]).objectLat("lat").objectLng("lng").objectAltitude("alt").objectThreeObject(() => model);

  const stationObj = { kind: "station", lat: station.lat, lng: station.lon, alt: 0.002 };
  const satLabel = { kind: "sat", lat: 0, lng: 0, alt: 0.13 };
  globe.htmlElementsData([stationObj]).htmlLat("lat").htmlLng("lng").htmlAltitude("alt").htmlTransitionDuration(0)
    .htmlElement((d) => {
      const el = document.createElement("div");
      if (d.kind === "station") { el.className = "globe-station"; el.innerHTML = `<i></i>${station.label}`; }
      else { el.className = "globe-sat"; el.textContent = satName; }
      return el;
    });

  // Rings: RF level pulses from the station (none at level 0), plus a one-off amber ripple on a touch.
  const STATION_RING = { kind: "rf", lat: station.lat, lng: station.lon };
  let level = 0, shownLevel = -1, touchRing = null;
  function setRings() {
    const rings = [];
    if (level > 0 && !reduced.matches) rings.push({ ...STATION_RING, level });
    if (touchRing) rings.push(touchRing);
    globe.ringsData(rings);
  }
  globe.ringAltitude(0.003)
    .ringColor((d) => (d.kind === "touch"
      ? (t) => `rgba(255,180,84,${1 - t})`
      : (t) => `rgba(79,209,255,${(1 - t) * (0.3 + 0.7 * d.level)})`))
    .ringMaxRadius((d) => (d.kind === "touch" ? 4 : 5))
    .ringPropagationSpeed((d) => (d.kind === "touch" ? 3 : 1 + 2 * d.level))
    .ringRepeatPeriod((d) => (d.kind === "touch" ? 60000 : 1400 - 800 * d.level));

  // Beam from the station to the satellite, only while it is above the horizon.
  const beam = new THREE.Mesh(
    new THREE.CylinderGeometry(0.14, 0.14, 1, 10, 1, true),
    new THREE.MeshBasicMaterial({ color: 0x4fd1ff, transparent: true, opacity: 0.4 }),
  );
  beam.visible = false;
  globe.scene().add(beam);
  const vec = (c) => new THREE.Vector3(c.x, c.y, c.z);

  let tracker = null, offsetMs = 0, follow = false, lastTrack = 0, timer = 0;
  const home = (ms) => globe.pointOfView({ lat: station.lat - 4, lng: station.lon, altitude: small ? 2.6 : 2.2 }, ms);
  home(0);

  // The satellite moves about 7 km/s, so its position is recomputed 10 times a second for smooth motion.
  function step() {
    const t = new Date(Date.now() + offsetMs);
    const s = subsolar(t);
    material.uniforms.sunPosition.value.set(s.lon, s.lat);
    const pov = globe.pointOfView();
    material.uniforms.globeRotation.value.set(pov.lng, pov.lat);
    const p = tracker?.positionAt(t);
    if (!p) {
      globe.objectsData([]);
      globe.htmlElementsData([stationObj]);
      globe.pathsData([]);
      beam.visible = false;
      return;
    }
    satObj.lat = satLabel.lat = p.lat;
    satObj.lng = satLabel.lng = p.lon;
    satObj.alt = satLabel.alt = p.altKm / R_KM;
    globe.objectsData([satObj]);
    globe.htmlElementsData([stationObj, satLabel]);
    beam.visible = p.elDeg > 0;
    if (beam.visible) {
      const a = vec(globe.getCoords(station.lat, station.lon, 0.002));
      const b = vec(globe.getCoords(p.lat, p.lon, satObj.alt));
      beam.position.copy(a.clone().add(b).multiplyScalar(0.5));
      beam.scale.set(1, a.distanceTo(b), 1);
      beam.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), b.clone().sub(a).normalize());
      beam.material.opacity = 0.4 + 0.55 * level;
    }
    if (t - lastTrack > 60e3) {
      lastTrack = t;
      const pts = tracker.track(t, 50, 50, 30);
      globe.pathsData([
        { pts: pts.filter((q) => q.t <= 0), color: "rgba(79,209,255,0.35)", dash: 0 },
        { pts: pts.filter((q) => q.t >= 0), color: "rgba(79,209,255,0.95)", dash: 0.015 },
      ]).pathPoints("pts").pathPointLat("lat").pathPointLng("lon").pathPointAlt(() => 0.004)
        .pathColor("color").pathStroke(2).pathDashLength((d) => d.dash || 1).pathDashGap((d) => (d.dash ? 0.01 : 0))
        .pathTransitionDuration(0);
    }
    if (follow && t.getSeconds() !== step.lastFollowSec) {
      step.lastFollowSec = t.getSeconds();
      globe.pointOfView({ lat: p.lat, lng: p.lon, altitude: 1.4 }, reduced.matches ? 0 : 1000);
    }
  }
  const start = () => { if (!timer) timer = setInterval(step, 100); };
  const stop = () => { clearInterval(timer); timer = 0; };
  start();

  return {
    update({ tracker: tr, now, rfLevel }) {
      tracker = tr || null;
      offsetMs = now - Date.now();
      level = rfLevel || 0;
      // Rebuild the rings only when the level changes noticeably, so the pulse does not restart every second.
      const q = Math.round(level * 10) / 10;
      if (q !== shownLevel) { shownLevel = q; level = q; setRings(); }
    },
    ripple() {
      if (reduced.matches) return;
      touchRing = { kind: "touch", lat: station.lat, lng: station.lon };
      setRings();
      setTimeout(() => { touchRing = null; setRings(); }, 1500);
    },
    setFollow(on) { follow = on; if (!on) home(1000); },
    reset() { follow = false; home(1000); },
    pause() { stop(); globe.pauseAnimation(); },
    resume() { globe.resumeAnimation(); start(); },
  };
}
