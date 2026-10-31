// 3D globe (CesiumJS, loaded lazily from a pinned CDN build). Bundled Natural Earth imagery only:
// no Cesium ion token, no Google key, and every widget that would call ion is turned off.
import { footprintRadiusKm } from "../lib/orbit.js";

const CESIUM_VERSION = "1.146.0";
const BASE = `https://cdn.jsdelivr.net/npm/cesium@${CESIUM_VERSION}/Build/Cesium/`;

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src; s.async = true; s.onload = resolve;
    s.onerror = () => reject(new Error("could not load the globe library"));
    document.head.append(s);
  });
}

function loadCss(href) {
  const l = document.createElement("link");
  l.rel = "stylesheet"; l.href = href;
  document.head.append(l);
}

export async function createGlobe(container, { station, satName }) {
  window.CESIUM_BASE_URL = BASE;
  loadCss(`${BASE}Widgets/widgets.css`);
  await loadScript(`${BASE}Cesium.js`);
  const Cs = window.Cesium;
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const small = matchMedia("(max-width: 699px)").matches;

  const viewer = new Cs.Viewer(container, {
    baseLayer: Cs.ImageryLayer.fromProviderAsync(
      Cs.TileMapServiceImageryProvider.fromUrl(Cs.buildModuleUrl("Assets/Textures/NaturalEarthII")),
    ),
    baseLayerPicker: false, geocoder: false, homeButton: false, sceneModePicker: false,
    navigationHelpButton: false, animation: false, timeline: false, fullscreenButton: false,
    infoBox: false, selectionIndicator: false, scene3DOnly: true,
    // Render only when something changes; the animation loop below asks for frames when needed.
    requestRenderMode: true, maximumRenderTimeChange: Infinity,
  });
  viewer.cesiumWidget.screenSpaceEventHandler.removeInputAction(Cs.ScreenSpaceEventType.LEFT_DOUBLE_CLICK);
  const scene = viewer.scene;
  scene.globe.maximumScreenSpaceError = small ? 4 : 2; // lighter on phones
  scene.backgroundColor = Cs.Color.fromCssColorString("#0a0e14");

  const CYAN = Cs.Color.fromCssColorString("#4fd1ff");
  const AMBER = Cs.Color.fromCssColorString("#ffb454");
  const font = '13px "Barlow Semi Condensed", sans-serif';
  const stationPos = Cs.Cartesian3.fromDegrees(station.lon, station.lat, 0);
  const labelStyle = { font, fillColor: Cs.Color.WHITE, outlineColor: Cs.Color.BLACK, outlineWidth: 3,
    style: Cs.LabelStyle.FILL_AND_OUTLINE, pixelOffset: new Cs.Cartesian2(0, -18) };

  viewer.entities.add({
    position: stationPos,
    point: { pixelSize: 9, color: AMBER, outlineColor: Cs.Color.BLACK, outlineWidth: 2 },
    label: { text: station.label, ...labelStyle },
  });
  const sat = viewer.entities.add({
    show: false,
    point: { pixelSize: 10, color: CYAN, outlineColor: Cs.Color.BLACK, outlineWidth: 2 },
    label: { text: satName, ...labelStyle },
  });
  const pastTrack = viewer.entities.add({ show: false, polyline: { width: 1.5, arcType: Cs.ArcType.GEODESIC, material: CYAN.withAlpha(0.3) } });
  const nextTrack = viewer.entities.add({ show: false, polyline: { width: 2, arcType: Cs.ArcType.GEODESIC,
    material: new Cs.PolylineDashMaterialProperty({ color: CYAN.withAlpha(0.85), dashLength: 12 }) } });
  const footprint = viewer.entities.add({ show: false, ellipse: { semiMajorAxis: 1, semiMinorAxis: 1, height: 0,
    material: CYAN.withAlpha(0.05), outline: true, outlineColor: CYAN.withAlpha(0.35) } });
  let level = 0;
  const beamMaterial = new Cs.PolylineGlowMaterialProperty({ glowPower: 0.25, color: CYAN.withAlpha(0.4) });
  const beam = viewer.entities.add({ show: false, polyline: { width: 6, arcType: Cs.ArcType.NONE, material: beamMaterial } });

  // Rings pulse out from the station; rate and brightness follow the RF level. None at level 0.
  const RING_MAX_M = 650e3;
  const t0 = performance.now();
  const ringPhase = (i) => {
    const period = 3.2 - 1.8 * level; // seconds
    return (((performance.now() - t0) / 1000 / period) + i / 3) % 1;
  };
  const rings = [0, 1, 2].map((i) => {
    const radius = new Cs.CallbackProperty(() => (reduced.matches ? 120e3 + 480e3 * level : 1 + ringPhase(i) * RING_MAX_M), false);
    return viewer.entities.add({
      position: stationPos, show: false,
      ellipse: { semiMajorAxis: radius, semiMinorAxis: radius, height: 0, fill: false, outline: true,
        outlineColor: new Cs.CallbackProperty(() => CYAN.withAlpha(reduced.matches
          ? 0.6 * level : (1 - ringPhase(i)) * (0.25 + 0.7 * level)), false) },
    });
  });

  // Touch ripple: one amber ring, 1.2 s.
  let rippleStart = -Infinity;
  const rippleT = () => (performance.now() - rippleStart) / 1200;
  const rippleR = new Cs.CallbackProperty(() => 1 + Math.min(1, rippleT()) * 450e3, false);
  const ripple = viewer.entities.add({ position: stationPos, show: false,
    ellipse: { semiMajorAxis: rippleR, semiMinorAxis: rippleR, height: 0, fill: false, outline: true,
      outlineColor: new Cs.CallbackProperty(() => AMBER.withAlpha(Math.max(0, 1 - rippleT())), false) } });

  let raf = 0;
  function animating() {
    return (level > 0 && !reduced.matches) || rippleT() < 1;
  }
  function loop() {
    ripple.show = rippleT() < 1;
    scene.requestRender();
    raf = animating() ? requestAnimationFrame(loop) : 0;
  }
  const kick = () => { if (!raf) raf = requestAnimationFrame(loop); };

  // Camera
  let follow = false;
  const home = () => viewer.camera.setView({ destination: Cs.Cartesian3.fromDegrees(station.lon, station.lat - 6, 15e6) });
  home();

  let lastTrackAt = 0;
  return {
    update({ pos, tracker, now, rfLevel }) {
      level = rfLevel || 0;
      rings.forEach((r) => { r.show = level > 0; });
      if (!pos) {
        sat.show = pastTrack.show = nextTrack.show = footprint.show = beam.show = false;
      } else {
        const satPos = Cs.Cartesian3.fromDegrees(pos.lon, pos.lat, pos.altKm * 1000);
        sat.position = satPos;
        sat.show = true;
        const fr = footprintRadiusKm(pos.altKm) * 1000;
        footprint.position = Cs.Cartesian3.fromDegrees(pos.lon, pos.lat, 0);
        footprint.ellipse.semiMajorAxis = fr;
        footprint.ellipse.semiMinorAxis = fr;
        footprint.show = true;
        beam.show = pos.elDeg > 0;
        if (beam.show) {
          beam.polyline.positions = [stationPos, satPos];
          beamMaterial.color = CYAN.withAlpha(0.35 + 0.65 * level);
        }
        if (now - lastTrackAt > 60e3) {
          lastTrackAt = now;
          const pts = tracker.track(new Date(now), 50, 50, 30);
          const toCart = (p) => Cs.Cartesian3.fromDegrees(p.lon, p.lat, 3000);
          pastTrack.polyline.positions = pts.filter((p) => p.t <= 0).map(toCart);
          nextTrack.polyline.positions = pts.filter((p) => p.t >= 0).map(toCart);
          pastTrack.show = nextTrack.show = true;
        }
        if (follow) viewer.camera.setView({ destination: Cs.Cartesian3.fromDegrees(pos.lon, pos.lat, 9e6) });
      }
      scene.requestRender();
      if (animating()) kick();
    },
    ripple() {
      if (reduced.matches) return;
      rippleStart = performance.now();
      kick();
    },
    setFollow(on) { follow = on; if (!on) home(); scene.requestRender(); },
    reset() { follow = false; home(); scene.requestRender(); },
    pause() { viewer.useDefaultRenderLoop = false; },
    resume() { viewer.useDefaultRenderLoop = true; scene.requestRender(); },
  };
}
