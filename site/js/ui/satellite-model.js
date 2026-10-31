// Illustrative 3D model of a polar weather satellite (not an exact Meteor-M2-4 replica, not to scale):
// gold-foil bus, two three-panel solar wings, instrument deck and a helix antenna facing Earth.
// +Z points toward Earth (nadir), wings run along X.
export function buildSatellite(THREE) {
  const g = new THREE.Group();
  const foil = new THREE.MeshStandardMaterial({ color: 0xd4a531, metalness: 0.85, roughness: 0.32 });
  const white = new THREE.MeshStandardMaterial({ color: 0xdfe4ea, metalness: 0.2, roughness: 0.55 });
  const cells = new THREE.MeshStandardMaterial({ color: 0x1b3a86, metalness: 0.55, roughness: 0.28, emissive: 0x081633, emissiveIntensity: 0.6 });
  const metal = new THREE.MeshStandardMaterial({ color: 0x9aa6b2, metalness: 0.8, roughness: 0.35 });

  const bus = new THREE.Mesh(new THREE.BoxGeometry(1.3, 1.3, 2.4), foil);
  g.add(bus);
  const deck = new THREE.Mesh(new THREE.CylinderGeometry(0.62, 0.72, 0.5, 20), white);
  deck.rotation.x = Math.PI / 2;
  deck.position.z = 1.45;
  g.add(deck);
  const helix = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.09, 0.9, 10), metal);
  helix.rotation.x = Math.PI / 2;
  helix.position.set(0.35, 0.35, 2.05);
  g.add(helix);
  const top = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.9, 0.2), white);
  top.position.z = -1.3;
  g.add(top);

  for (const side of [-1, 1]) {
    const boom = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.05, 0.9, 8), metal);
    boom.rotation.z = Math.PI / 2;
    boom.position.x = side * 1.1;
    g.add(boom);
    for (let i = 0; i < 3; i++) {
      const panel = new THREE.Mesh(new THREE.BoxGeometry(1.35, 0.05, 2.0), cells);
      panel.position.x = side * (1.6 + i * 1.42);
      g.add(panel);
      const rib = new THREE.Mesh(new THREE.BoxGeometry(1.35, 0.07, 0.06), metal);
      rib.position.set(panel.position.x, 0, 0);
      g.add(rib);
    }
  }
  return g;
}
