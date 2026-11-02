// 3D view of the radio link (SPEC 16.3). The station sits at the origin; the tower (the NanoVNA's stand-in) stands at
// the station's own distance estimate, with the estimate's range shaded on the ground. Radio waves follow the real
// signal level and stop at level 0. Ground distances are to scale (1 unit = 1 m); the models are enlarged so they can
// be seen next to a 15 m line. Nothing here invents a number: with no estimate the tower is shown faded, "no estimate".
const LIB_URL = new URL("../../vendor/scene.lib.js", import.meta.url).href;   // self-hosted, see tools/vendor.mjs

const COLORS = { bg: 0x0a0e14, ground: 0x0d131c, line: 0x1f2b3a, muted: 0x7d8fa3, text: 0xd6e2f0, accent: 0x4fd1ff,
  amber: 0xffb454, good: 0x5ee09a, bad: 0xff5c6a, steel: 0xb8c4d2, box: 0x2a3442 };
const BAND_COLOR = { nosignal: COLORS.muted, weak: COLORS.amber, good: COLORS.accent, strong: COLORS.good, none: COLORS.muted };
const RINGS = [1, 5, 10, 15, 20];
const BEARING = Math.atan2(-0.45, 1);   // the tower sits along this direction from the station
const PARK_M = 12;                      // where the faded tower waits when there is no estimate
const TOWER_H = 5.5, ANT_H = 2.35;      // tower top and station antenna heights (enlarged models)

export async function createScene(container, labelsEl, { onError } = {}) {
  const { THREE, OrbitControls } = await import(LIB_URL);
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const phone = matchMedia("(max-width: 699px)").matches;

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, phone ? 1.5 : 2));
  renderer.setClearColor(COLORS.bg);
  container.prepend(renderer.domElement);

  const scene = new THREE.Scene();
  scene.fog = new THREE.Fog(COLORS.bg, 32, 75);
  const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 200);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.maxPolarAngle = Math.PI * 0.47;   // stay above the ground
  controls.minDistance = 4;
  controls.maxDistance = 60;
  let lastUserMove = -1e9;
  controls.addEventListener("start", () => { lastUserMove = performance.now(); });

  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x0b0f14, 1.1));
  const sun = new THREE.DirectionalLight(0xffffff, 1.4);
  sun.position.set(-8, 14, 10);
  scene.add(sun);

  const mat = (color, extra = {}) => new THREE.MeshStandardMaterial({ color, roughness: 0.7, metalness: 0.2, ...extra });
  const along = (m, y = 0) => new THREE.Vector3(Math.cos(BEARING) * m, y, Math.sin(BEARING) * m);

  // ----- Ground: disk, distance rings, faint radial lines -----
  const ground = new THREE.Mesh(new THREE.CircleGeometry(26, 96), mat(COLORS.ground, { roughness: 1, metalness: 0 }));
  ground.rotation.x = -Math.PI / 2;
  scene.add(ground);
  const ringMat = new THREE.LineBasicMaterial({ color: COLORS.line });
  const ringMatStrong = new THREE.LineBasicMaterial({ color: 0x33465c });
  for (const r of RINGS) {
    const pts = [];
    for (let i = 0; i <= 128; i++) { const a = (i / 128) * Math.PI * 2; pts.push(new THREE.Vector3(Math.cos(a) * r, 0.01, Math.sin(a) * r)); }
    scene.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), r % 5 === 0 ? ringMatStrong : ringMat));
  }
  for (let k = 0; k < 12; k++) {
    const a = (k / 12) * Math.PI * 2;
    const pts = [new THREE.Vector3(Math.cos(a) * 1, 0.01, Math.sin(a) * 1), new THREE.Vector3(Math.cos(a) * 25, 0.01, Math.sin(a) * 25)];
    scene.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), ringMat));
  }

  // ----- The station: weatherproof box with its LCD and touch pad, a mast, and the V-dipole at 120 degrees -----
  const station = new THREE.Group();
  const box = new THREE.Mesh(new THREE.BoxGeometry(1.1, 0.7, 0.8), mat(COLORS.box));
  box.position.y = 0.45;
  station.add(box);
  for (const [x, z] of [[-0.45, -0.3], [0.45, -0.3], [-0.45, 0.3], [0.45, 0.3]]) {
    const foot = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.06, 0.1, 10), mat(0x111820));
    foot.position.set(x, 0.05, z);
    station.add(foot);
  }
  // The LCD shows the same two lines as the real one (page 0), drawn on a canvas.
  const lcdCanvas = document.createElement("canvas");
  lcdCanvas.width = 256; lcdCanvas.height = 64;
  const lcdCtx = lcdCanvas.getContext("2d");
  const lcdTex = new THREE.CanvasTexture(lcdCanvas);
  lcdTex.colorSpace = THREE.SRGBColorSpace;
  const lcd = new THREE.Mesh(new THREE.PlaneGeometry(0.72, 0.18), new THREE.MeshBasicMaterial({ map: lcdTex }));
  lcd.position.set(0, 0.52, 0.401);
  station.add(lcd);
  const pad = new THREE.Mesh(new THREE.BoxGeometry(0.28, 0.01, 0.22), mat(0xb87333, { metalness: 0.7, roughness: 0.35 }));
  pad.position.set(0.32, 0.805, 0.2);
  station.add(pad);
  const fan = new THREE.Mesh(new THREE.CircleGeometry(0.17, 24), mat(0x161d27));
  fan.position.set(0.551, 0.45, 0);
  fan.rotation.y = Math.PI / 2;
  station.add(fan);
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 1.6, 10), mat(COLORS.steel, { metalness: 0.6 }));
  mast.position.set(-0.4, 0.8 + 0.75, -0.25);
  station.add(mast);
  const feed = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.08, 0.12), mat(0x1b222c));
  feed.position.set(-0.4, ANT_H, -0.25);
  station.add(feed);
  const armMat = mat(COLORS.steel, { metalness: 0.8, roughness: 0.3, emissive: 0x000000 });
  for (const a of [BEARING + Math.PI / 2 - Math.PI / 3, BEARING + Math.PI / 2 + Math.PI / 3]) {
    // Two arms 120 degrees apart in the horizontal plane, opening toward the tower's side (53.4 cm each, drawn x2).
    const arm = new THREE.Mesh(new THREE.CylinderGeometry(0.02, 0.02, 1.07, 8), armMat);
    arm.rotation.z = Math.PI / 2;
    arm.rotation.y = -a;
    arm.position.set(-0.4 + Math.cos(a) * 0.535, ANT_H, -0.25 + Math.sin(a) * 0.535);
    station.add(arm);
  }
  scene.add(station);
  const antennaPos = new THREE.Vector3(-0.4, ANT_H, -0.25);

  // ----- The tower: tapered lattice, red and white legs, a beacon and a whip on top -----
  const tower = new THREE.Group();
  const legW0 = 0.9, legW1 = 0.18;
  const corner = (y, sx, sz) => { const w = legW0 + (legW1 - legW0) * (y / TOWER_H); return new THREE.Vector3(sx * w, y, sz * w); };
  const corners = [[1, 1], [-1, 1], [-1, -1], [1, -1]];
  const segs = 7;
  const towerMats = [];
  for (const [sx, sz] of corners) {
    for (let i = 0; i < segs; i++) {
      const a = corner((i * TOWER_H) / segs, sx, sz), b = corner(((i + 1) * TOWER_H) / segs, sx, sz);
      const len = a.distanceTo(b);
      const m = mat(i % 2 ? 0xf2f2f2 : 0xd0283a, { transparent: true });
      towerMats.push(m);
      const leg = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, len, 6), m);
      leg.position.copy(a).lerp(b, 0.5);
      leg.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), b.clone().sub(a).normalize());
      tower.add(leg);
    }
  }
  const brace = [];
  for (let i = 0; i < segs; i++) {
    const y0 = (i * TOWER_H) / segs, y1 = ((i + 1) * TOWER_H) / segs;
    for (let f = 0; f < 4; f++) {
      const [ax, az] = corners[f], [bx, bz] = corners[(f + 1) % 4];
      brace.push(corner(y0, ax, az), corner(y1, bx, bz), corner(y1, ax, az), corner(y0, bx, bz), corner(y1, ax, az), corner(y1, bx, bz));
    }
  }
  const braceMat = new THREE.LineBasicMaterial({ color: 0x9aa7b6, transparent: true });
  tower.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(brace), braceMat));
  const padBase = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.15, 2.4), mat(0x3a414b, { transparent: true }));
  padBase.position.y = 0.075;
  tower.add(padBase);
  towerMats.push(padBase.material);
  const whip = new THREE.Mesh(new THREE.CylinderGeometry(0.02, 0.02, 1.2, 6), mat(COLORS.steel, { transparent: true }));
  whip.position.y = TOWER_H + 0.6;
  tower.add(whip);
  towerMats.push(whip.material);
  const beaconMat = new THREE.MeshBasicMaterial({ color: COLORS.bad, transparent: true });
  const beacon = new THREE.Mesh(new THREE.SphereGeometry(0.16, 16, 12), beaconMat);
  beacon.position.y = TOWER_H + 0.05;
  tower.add(beacon);
  scene.add(tower);

  // ----- The link: dashed line, the estimate's range on the ground, travelling signal dots, ground ripples -----
  const linkMat = new THREE.LineDashedMaterial({ color: COLORS.muted, dashSize: 0.4, gapSize: 0.3, transparent: true });
  const link = new THREE.Line(new THREE.BufferGeometry(), linkMat);
  scene.add(link);
  const bandMat = new THREE.MeshBasicMaterial({ color: COLORS.accent, transparent: true, opacity: 0.16, side: THREE.DoubleSide, depthWrite: false });
  const band = new THREE.Mesh(new THREE.BufferGeometry(), bandMat);
  band.rotation.x = -Math.PI / 2;
  band.position.y = 0.02;
  scene.add(band);
  const dots = [];
  for (let i = 0; i < 7; i++) {
    const d = new THREE.Mesh(new THREE.SphereGeometry(0.08, 10, 8), new THREE.MeshBasicMaterial({ color: COLORS.accent, transparent: true, opacity: 0 }));
    d.userData.t = i / 7;
    scene.add(d);
    dots.push(d);
  }
  const ripples = [];
  for (let i = 0; i < 3; i++) {
    const r = new THREE.Mesh(new THREE.RingGeometry(0.92, 1, 64), new THREE.MeshBasicMaterial({ color: COLORS.accent, transparent: true, opacity: 0, side: THREE.DoubleSide, depthWrite: false }));
    r.rotation.x = -Math.PI / 2;
    r.position.y = 0.03;
    r.userData.phase = i / 3;
    scene.add(r);
    ripples.push(r);
  }

  // ----- Labels: HTML on top of the canvas, placed by projecting points every frame -----
  const label = (cls, text = "") => { const el = document.createElement("span"); el.className = `scene-label ${cls}`; el.textContent = text; labelsEl.append(el); return el; };
  const lStation = label("lbl-station", "Station");
  const lTower = label("lbl-tower", "Tower");
  const lDist = label("lbl-dist");
  const ringLabels = RINGS.map((r) => ({ r, el: label("lbl-ring", `${r} m`) }));
  const ringLabelAngle = BEARING - Math.PI * 0.62;

  // ----- State -----
  const st = { target: null, shown: PARK_M, lo: null, hi: null, level: 0, detected: false, band: "none", offline: false };
  let bandKey = "";

  function drawLcd(lines) {
    lcdCtx.fillStyle = "#3a6b22";
    lcdCtx.fillRect(0, 0, 256, 64);
    lcdCtx.fillStyle = "#d9f7b0";
    lcdCtx.font = "bold 22px 'IBM Plex Mono', ui-monospace, monospace";
    lcdCtx.textBaseline = "top";
    lcdCtx.fillText((lines[0] ?? "").slice(0, 16), 8, 6);
    lcdCtx.fillText((lines[1] ?? "").slice(0, 16), 8, 34);
    lcdTex.needsUpdate = true;
  }
  drawLcd(["V-Dipole Station", "waiting for data"]);

  function rebuildBand() {
    const key = `${st.lo}|${st.hi}|${st.band}`;
    if (key === bandKey) return;
    bandKey = key;
    band.geometry.dispose();
    if (st.lo === null || st.hi === null) { band.geometry = new THREE.BufferGeometry(); return; }
    // A wedge from the low to the high estimate, 24 degrees wide, centred on the tower's direction.
    band.geometry = new THREE.RingGeometry(Math.max(0.3, st.lo), Math.max(st.lo + 0.05, st.hi), 48, 1, -BEARING - 0.21, 0.42);
    bandMat.color.setHex(BAND_COLOR[st.band] ?? COLORS.accent);
  }

  // A three-quarter side view of the link that keeps both ends in frame at any distance.
  const linkDir = along(1).normalize();
  const sideDir = new THREE.Vector3(-linkDir.z, 0, linkDir.x);
  const viewDir = sideDir.clone().multiplyScalar(0.85).addScaledVector(linkDir, -0.35).add(new THREE.Vector3(0, 0.42, 0)).normalize();
  function frame(immediate) {
    const d = st.shown;
    const mid = along(d / 2, 1.4);
    controls.target.copy(mid);
    const aspect = Math.max(0.6, camera.aspect);
    const dist = Math.max(13, ((d + 5) * 1.3) / Math.min(1.5, aspect));
    const dir = viewDir.clone();
    const pos = mid.clone().add(dir.multiplyScalar(dist));
    if (immediate) camera.position.copy(pos);
    else camera.position.lerp(pos, 0.04);
  }

  function resize() {
    const w = container.clientWidth || 600, h = container.clientHeight || 360;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(container);
  resize();
  frame(true);

  const v = new THREE.Vector3();
  function place(el, p, visible = true) {
    v.copy(p).project(camera);
    const behind = v.z > 1;
    el.hidden = !visible || behind;
    if (el.hidden) return;
    el.style.transform = `translate(${((v.x + 1) / 2) * container.clientWidth}px, ${((1 - v.y) / 2) * container.clientHeight}px) translate(-50%, -50%)`;
  }

  let running = true, raf = 0, last = performance.now(), t = 0, lastFrameTarget = PARK_M;
  function loop(now) {
    raf = requestAnimationFrame(loop);
    const dt = Math.min(0.1, (now - last) / 1000);
    last = now;
    const motion = !reduced.matches;
    t += motion ? dt : 0;

    // Tower position: glide to the estimate (jump with reduced motion); park faded when there is none.
    const goal = st.target ?? st.shown ?? PARK_M;
    st.shown = motion ? st.shown + (goal - st.shown) * Math.min(1, dt * 2.2) : goal;
    tower.position.copy(along(st.shown));
    const known = st.target !== null && !st.offline;
    const alpha = known ? 1 : 0.35;
    for (const m of towerMats) m.opacity = alpha;
    braceMat.opacity = alpha;
    beaconMat.opacity = st.offline ? 0.25 : motion ? (Math.sin(t * 3.2) > 0.2 ? 1 : 0.18) : 1;

    // The dashed line from tower top to antenna.
    const top = along(st.shown, TOWER_H);
    link.geometry.dispose();
    link.geometry = new THREE.BufferGeometry().setFromPoints([top, antennaPos]);
    link.computeLineDistances();
    linkMat.opacity = known ? 0.9 : 0.35;
    rebuildBand();

    // Signal dots and ripples follow the real level (none at 0, none while offline).
    const level = st.offline ? 0 : st.level;
    const color = BAND_COLOR[st.band] ?? COLORS.accent;
    for (const d of dots) {
      d.userData.t = (d.userData.t + dt * (0.12 + 0.45 * level)) % 1;
      const u = motion ? d.userData.t : (dots.indexOf(d) + 0.5) / dots.length;
      d.position.copy(top).lerp(antennaPos, u);
      d.position.y += Math.sin(u * Math.PI) * 0.6;
      d.material.color.setHex(color);
      d.material.opacity = level > 0 ? 0.35 + 0.65 * level : 0;
    }
    for (const r of ripples) {
      const ph = motion ? (r.userData.phase + t * (0.25 + 0.35 * level)) % 1 : r.userData.phase;
      const s = 0.6 + ph * 4.5;
      r.scale.set(s, s, s);
      r.position.x = tower.position.x;
      r.position.z = tower.position.z;
      r.material.color.setHex(color);
      r.material.opacity = level > 0 ? (1 - ph) * 0.5 * level : 0;
    }
    armMat.emissive.setHex(st.detected && !st.offline ? color : 0x000000);
    armMat.emissiveIntensity = 0.8;

    // Keep the pair in view while nobody is steering the camera.
    if (motion && performance.now() - lastUserMove > 8000 && Math.abs(st.shown - lastFrameTarget) > 0.01) frame(false);
    if (Math.abs(goal - st.shown) < 0.01) lastFrameTarget = st.shown;
    controls.update();
    renderer.render(scene, camera);

    place(lStation, new THREE.Vector3(0, 3.0, 0));
    place(lTower, along(st.shown, TOWER_H + 1.6));
    lTower.textContent = known ? "Tower (NanoVNA)" : "Tower (no estimate)";
    lDist.textContent = known ? `≈ ${st.target < 10 ? st.target.toFixed(1) : Math.round(st.target)} m` : "";
    place(lDist, along(st.shown / 2, 0.3), known);
    for (const { r, el } of ringLabels) place(el, new THREE.Vector3(Math.cos(ringLabelAngle) * r, 0.05, Math.sin(ringLabelAngle) * r));
  }
  raf = requestAnimationFrame(loop);

  return {
    update({ distM = null, distLoM = null, distHiM = null, level = 0, detected = false, band: b = "none", offline = false, lcd = null } = {}) {
      st.target = Number.isFinite(distM) ? distM : null;
      st.lo = Number.isFinite(distLoM) ? distLoM : null;
      st.hi = Number.isFinite(distHiM) ? distHiM : null;
      if (st.target === null) { st.lo = st.hi = null; }
      st.level = Math.min(1, Math.max(0, level || 0));
      st.detected = detected;
      st.band = b;
      st.offline = offline;
      if (lcd) drawLcd(lcd);
    },
    reset() { lastUserMove = -1e9; frame(true); },
    pause() { if (running) { running = false; cancelAnimationFrame(raf); } },
    resume() { if (!running) { running = true; last = performance.now(); raf = requestAnimationFrame(loop); } },
    get state() { return { ...st }; },
    onError,
  };
}
