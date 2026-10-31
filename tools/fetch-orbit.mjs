// Downloads current orbital elements (TLE) from CelesTrak for every satellite in site/js/config.js
// and writes site/data/gp.json. Run by the deploy workflow; can also be run by hand:
//   node tools/fetch-orbit.mjs
// CelesTrak asks users not to download the same data more often than every 2 hours. The workflow runs every 6.
import { writeFileSync } from "node:fs";
import { CONFIG } from "../site/js/config.js";

const OUT = new URL("../site/data/gp.json", import.meta.url);

// TLE line checksum: sum of digits, minus signs count as 1, modulo 10.
export function tleChecksumOk(line) {
  if (!/^[12] /.test(line) || line.length !== 69) return false;
  let sum = 0;
  for (const ch of line.slice(0, 68)) sum += ch === "-" ? 1 : /\d/.test(ch) ? Number(ch) : 0;
  return sum % 10 === Number(line[68]);
}

async function fetchTle(noradId) {
  const url = `https://celestrak.org/NORAD/elements/gp.php?CATNR=${noradId}&FORMAT=TLE`;
  const res = await fetch(url, { headers: { "User-Agent": "vdipole-tracking-station (GitHub Actions)" } });
  if (!res.ok) throw new Error(`CelesTrak HTTP ${res.status} for ${noradId}`);
  const lines = (await res.text()).split(/\r?\n/).map((l) => l.trimEnd()).filter(Boolean);
  const tle1 = lines.find((l) => l.startsWith("1 "));
  const tle2 = lines.find((l) => l.startsWith("2 "));
  if (!tle1 || !tle2 || !tleChecksumOk(tle1) || !tleChecksumOk(tle2)) {
    throw new Error(`CelesTrak returned no valid TLE for ${noradId}`);
  }
  if (Number(tle1.slice(2, 7)) !== noradId) throw new Error(`TLE is for the wrong satellite (${tle1.slice(2, 7)})`);
  return { noradId, name: lines[0].startsWith("1 ") ? String(noradId) : lines[0].trim(), tle1, tle2 };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const satellites = [];
  for (const s of CONFIG.satellites) satellites.push(await fetchTle(s.noradId));
  const doc = { source: "CelesTrak GP data (celestrak.org)", fetchedAt: new Date().toISOString(), satellites };
  writeFileSync(OUT, JSON.stringify(doc, null, 2) + "\n");
  console.log(`Wrote ${satellites.length} satellite(s) to site/data/gp.json`);
}
