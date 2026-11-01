// Unit tests for tools/make-passes.mjs (the station's pass schedule, SPEC 14.8), using the committed orbit data.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as satlib from "../../site/vendor/satellite.lib.js";
import { CONFIG } from "../../site/js/config.js";
import { createTracker } from "../../site/js/lib/orbit.js";
import { makeSchedule, LIMITS } from "../../tools/make-passes.mjs";

const gp = JSON.parse(readFileSync(new URL("../../site/data/gp.json", import.meta.url), "utf8"));
const sat = gp.satellites.find((s) => s.noradId === CONFIG.satellites[0].noradId);
const tracker = createTracker(sat, CONFIG.station, satlib);
const from = new Date(tracker.epoch.getTime() + 3600e3);

test("passes: agrees with the site's own pass finder and fits the station", () => {
  const doc = makeSchedule(gp, from);
  const site = tracker.nextPasses(from, 24, 48, 5);
  assert.equal(doc.passes.length, site.length);
  assert.ok(doc.passes.length >= 6 && doc.passes.length <= LIMITS.maxPasses, `${doc.passes.length} passes in 48 h`);
  doc.passes.forEach((p, i) => {
    assert.ok(Math.abs(p.aos - site[i].aos.getTime() / 1000) <= 1, "same start");
    assert.ok(Math.abs(p.los - site[i].los.getTime() / 1000) <= 1, "same end");
    assert.ok(p.los > p.aos && p.el.length >= 2 && p.el.length <= LIMITS.maxPoints);
    assert.equal(p.el.length, Math.ceil((p.los - p.aos) / 30) + 1, "one point every 30 s plus the end");
    assert.ok(Math.abs(p.el[0] - 5) <= 0.3 && Math.abs(p.el.at(-1) - 5) <= 0.3, "starts and ends near 5 degrees");
    assert.ok(Math.max(...p.el) <= p.maxEl + 0.05);
    if (i) assert.ok(p.aos > doc.passes[i - 1].los, "in order, no overlap");
  });
  assert.equal(doc.validUntil - doc.generated, 48 * 3600);
  assert.ok(JSON.stringify(doc).length < 6000, "small");
  // The longest pass above 5 degrees is about 13 minutes (SPEC 14.5, the freeze limit).
  assert.ok(Math.max(...doc.passes.map((p) => p.los - p.aos)) < 14 * 60);
});

test("passes: a pass in progress starts at the file's start time", () => {
  const first = tracker.nextPasses(from, 1, 48, 5)[0];
  const mid = new Date((first.aos.getTime() + first.los.getTime()) / 2);
  const doc = makeSchedule(gp, mid);
  assert.equal(doc.passes[0].aos, doc.generated);
  assert.ok(doc.passes[0].el[0] > 5);
});

test("passes: old orbit data makes no schedule", () => {
  const late = new Date(tracker.epoch.getTime() + (CONFIG.orbit.staleDays + 1) * 86400e3);
  assert.throws(() => makeSchedule(gp, late), /days old/);
});
