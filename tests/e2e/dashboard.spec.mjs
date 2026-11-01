// Smoke tests against the labeled fixture source (tests/fixtures/fixture-source.js).
// Run: npm run test:e2e
import { test, expect } from "@playwright/test";

// Collect console errors, warnings and uncaught exceptions for every test.
test.beforeEach(async ({ page }, info) => {
  info.consoleProblems = [];
  page.on("console", (m) => {
    // "GL Driver Message ... GPU stall due to ReadPixels" comes from the software GPU (SwiftShader) that headless
    // Chromium uses in tests, not from page code: a probe confirmed the page never calls readPixels.
    if (/GL Driver Message .*GPU stall due to ReadPixels/.test(m.text())) return;
    if (m.type() === "error" || m.type() === "warning") info.consoleProblems.push(`${m.type()}: ${m.text()}`);
  });
  page.on("pageerror", (e) => info.consoleProblems.push(`pageerror: ${e.message}`));
});

async function open(page, query = "?autoplay=0") {
  await page.goto(`/${query}`);
  await page.waitForFunction(() => document.documentElement.dataset.ready === "1" && window.__fixture);
}

const latest = (page, patch, opts) => page.evaluate(([p, o]) => window.__fixture.setLatest(p, o), [patch, opts ?? {}]);

test("loads with no console errors and shows the fixture label", async ({ page }, info) => {
  await open(page, "");
  await expect(page.locator("#fixture-label")).toContainText("TEST FIXTURE DATA");
  await expect(page.locator("body")).toHaveAttribute("data-station", "live");
  await expect(page.locator("h1")).toHaveText("Satellite V-Dipole Tracking Station");
  // Give the lazy globe and the orbit module time to start, then check the console.
  await page.waitForTimeout(6000);
  expect(info.consoleProblems).toEqual([]);
});

test("old data shows OFFLINE banner, fresh data clears it", async ({ page }) => {
  await open(page);
  await latest(page, {}, { ageMs: 45_000 });
  await expect(page.locator("body")).toHaveAttribute("data-station", "offline", { timeout: 1500 });
  await expect(page.locator("#banner")).toBeVisible();
  await expect(page.locator("#banner")).toContainText("Station offline");
  await latest(page, {}, { ageMs: 10_000 });
  await expect(page.locator("body")).toHaveAttribute("data-station", "delayed");
  await latest(page, {});
  await expect(page.locator("body")).toHaveAttribute("data-station", "live");
  await expect(page.locator("#banner")).toBeHidden();
});

test("age counter ticks without new data and goes OFFLINE by itself", async ({ page }) => {
  await page.clock.install(); // before load, so the page's 1 s ticker runs on the controlled clock
  await open(page);
  await latest(page, {}, { ageMs: 28_000 });
  await expect(page.locator("body")).toHaveAttribute("data-station", "delayed");
  await page.clock.runFor(4000);
  await expect(page.locator("body")).toHaveAttribute("data-station", "offline");
});

test("battery widget states and no layout shift", async ({ page }) => {
  await open(page);
  const widget = page.locator(".batt-row");
  const width0 = (await widget.boundingBox()).width;
  const cases = [
    [100, 12.5, "green", "~100"], [80, 12.0, "green", "~80"], [50, 11.6, "green", "~50"],
    [35, 11.4, "amber", "~35"], [20, 11.2, "amber", "~20"], [5, 10.4, "red", "~5"], [0, 9.6, "red", "~0"],
  ];
  for (const [pct, v, band, text] of cases) {
    await latest(page, { battSocPct: pct, battV: v });
    await expect(page.locator("#batt")).toHaveAttribute("data-band", band);
    await expect(page.locator("#batt-pct")).toHaveText(text);
    expect(Math.abs((await widget.boundingBox()).width - width0)).toBeLessThan(1);
  }
  await expect(page.locator("#batt-warn")).toBeVisible();
  await latest(page, { battSocPct: null, battV: null, battHoursLeft: null });
  await expect(page.locator("#batt")).toHaveAttribute("data-band", "none");
  await expect(page.locator("#batt-pct")).toHaveText("--");
  await expect(page.locator("#batt-left")).toHaveText("--");
});

test("fan: spinning, stalled, not connected", async ({ page }) => {
  await open(page);
  await latest(page, { fanRpm: 1800, fanStalled: false });
  await expect(page.locator(".fan-panel")).toHaveAttribute("data-state", "spinning");
  await expect(page.locator("#fan-rpm")).toHaveText("1,800");
  await latest(page, { fanRpm: 120, fanStalled: true });
  await expect(page.locator(".fan-panel")).toHaveAttribute("data-state", "stalled");
  await latest(page, { fanRpm: null, fanStalled: false });
  await expect(page.locator(".fan-panel")).toHaveAttribute("data-state", "none");
  await expect(page.locator("#fan-state")).toHaveText("Not connected");
});

test("a touch event updates the card and highlights it", async ({ page }) => {
  await open(page);
  await page.evaluate(() => window.__fixture.setTouch({ state: true, count: 4 }));
  await expect(page.locator("#touch-state")).toHaveAttribute("data-state", "touched");
  await expect(page.locator("#touch-count")).toHaveText("4");
  await expect(page.locator("#touch-panel")).toHaveClass(/flash/);
  await page.evaluate(() => window.__fixture.setTouch({ state: false, count: 4 }));
  await expect(page.locator("#touch-state")).toHaveAttribute("data-state", "idle");
  await latest(page, { capFault: true });
  await expect(page.locator("#touch-label")).toHaveText("Sensor fault");
});

test("charts render, show gaps, and stay under the point budget", async ({ page }) => {
  await open(page);
  await page.locator('[data-range="24h"]').click();
  await page.locator("#tab-misc").click();
  const info = await page.waitForFunction(() => {
    const c = window.Chart?.getChart(document.querySelector('[data-chart="fan"] canvas'));
    const d = c?.data.datasets[0].data;
    return d && d.length > 100 ? { n: d.length, nulls: d.filter((p) => p.y === null).length } : null;
  });
  const { n, nulls } = await info.jsonValue();
  expect(n).toBeLessThanOrEqual(1500);
  expect(nulls).toBeGreaterThan(0); // fan null before it was wired, plus the 15 min outage
});

test("temperature unit toggle switches to Fahrenheit", async ({ page }) => {
  await open(page);
  await latest(page, { tempC: 20 });
  await expect(page.locator("#env-bmp")).toHaveText("20.0");
  await page.locator("#unit-toggle").click();
  await expect(page.locator("#env-bmp")).toHaveText("68.0");
  await expect(page.locator(".temp-unit").first()).toHaveText("°F");
});

test("satellite readouts and the globe canvas appear", async ({ page }) => {
  await open(page);
  await expect(page.locator("#sat-el")).toHaveText(/-?\d+\.\d°/);
  await expect(page.locator("#sat-next")).not.toHaveText("--");
  await expect(page.locator("#pass-rows tr")).toHaveCount(5);
  await expect(page.locator("#pass-rows tr").first()).toContainText(/\d\d:\d\d/);
  await expect(page.locator("#globe canvas")).toBeVisible({ timeout: 30_000 });
});

test("no horizontal scroll", async ({ page }) => {
  await open(page);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});

test("screenshots for review @screens", async ({ page }, info) => {
  await open(page, ""); // autoplay on, so the status reads LIVE
  await page.waitForTimeout(8000); // globe imagery
  await page.screenshot({ path: `tests/screenshots/${info.project.name}.png`, fullPage: true });
  await page.screenshot({ path: `tests/screenshots/${info.project.name}-first-view.png` });
});

// ---------- Reception quality score (SPEC 14.14) ----------
const scored = (over = {}) => ({
  rqs: 57.3, rqsLabel: "GOOD", rqsTag: "PASS", rfExcessDb: 6.2, rfBaselineDbm: -75.1, rfThresholdDbm: -73.9,
  rfSigmaDb: 0.3, rfDetected: true, passElDeg: 41.6, passCorr: 0.83, rqsTempC: 23.4,
  rqsSnr: 0.52, rqsDet: 1, rqsPass: 0.93, rqsHealth: 1, battSocPct: 70, ...over,
});

test("RQS hero: every label, tag and flag", async ({ page }) => {
  await open(page);
  const panel = page.locator("#rqs-panel");
  await expect(panel).toHaveAttribute("data-state", "scored");     // the fixture opens mid-pass

  await latest(page, scored());
  await expect(page.locator("#rqs-num")).toHaveText("57");
  await expect(page.locator("#rqs-label")).toHaveText("GOOD");
  await expect(panel).toHaveAttribute("data-band", "good");
  await expect(page.locator("#rqs-tag")).toHaveText("PASS");
  await expect(page.locator("#rqs-tagwords")).toHaveText("in a predicted pass");
  await expect(page.locator("#rqs-bar-snr")).toHaveText("0.52");
  await expect(page.locator("#rqs-bar-pass")).toHaveText("0.93");
  await expect(page.locator("#rqs-el")).toHaveText("42°");
  await expect(page.locator("#rqs-corr")).toHaveText("0.83");
  await expect(page.locator("#rqs-detected")).toBeVisible();
  await expect(page.locator("#rqs-temp-na")).toBeHidden();
  await expect(page.locator("#rf-excess")).toHaveText("+6.2");     // the S-meter follows the station's excess
  await expect(page.locator("#rf-base")).toHaveText("-75.1 dBm");

  for (const [rqs, label, band, tag] of [[3.1, "NO SIGNAL", "nosignal", "NO PASS"], [22.4, "WEAK", "weak", "NO SCHEDULE"],
    [70, "STRONG", "strong", "NO PASS"], [91.5, "STRONG", "strong", "TEST"]]) {
    await latest(page, scored({ rqs, rqsLabel: label, rqsTag: tag, passElDeg: null, passCorr: null }));
    await expect(page.locator("#rqs-label")).toHaveText(label);
    await expect(panel).toHaveAttribute("data-band", band);
    await expect(page.locator("#rqs-tag")).toHaveText(tag);
    await expect(page.locator("#rqs-el")).toHaveText("not in a pass");
  }

  await latest(page, scored({ rqsTempC: null, battSocPct: null }));
  await expect(page.locator("#rqs-temp-na")).toBeVisible();
  await expect(page.locator("#rqs-batt-na")).toBeVisible();

  await latest(page, scored({ rqs: null, rqsLabel: "WARMING UP", rfExcessDb: null, rfBaselineDbm: null, rfThresholdDbm: null,
    rfSigmaDb: null, rfDetected: false, rqsSnr: null, rqsDet: null, rqsPass: null, rqsHealth: null }));
  await expect(panel).toHaveAttribute("data-state", "warming");
  await expect(page.locator("#rqs-num")).toHaveText("--");
  await expect(page.locator("#rqs-label")).toHaveText("WARMING UP");
  await expect(page.locator("#rqs-bar-health")).toHaveText("--");
  await expect(page.locator("#rf-base")).toHaveText("learning");
  await expect(page.locator("#rf-excess")).toHaveText("--");

  // A record from firmware before 1.2 has no RQS fields at all.
  await latest(page, { rfFiltDbm: -75, rfRawDbm: -75.4, battSocPct: 60 }, { replace: true });
  await expect(panel).toHaveAttribute("data-state", "none");
  await expect(page.locator("#rqs-num")).toHaveText("--");
  await expect(page.locator("#rqs-tag")).toHaveText("--");

  await page.evaluate(() => window.__fixture.clearLatest());
  await expect(page.locator("#rqs-num")).toHaveText("--");

  await latest(page, scored(), { ageMs: 45_000 });
  await expect(page.locator("body")).toHaveAttribute("data-station", "offline", { timeout: 1500 });
  await expect(panel).toHaveAttribute("data-band", "none");          // grey while offline
  await expect(page.locator("#rqs-num")).toHaveText("57");          // last value still readable
});

test("RQS: why this score opens, explains, and closes", async ({ page }) => {
  await open(page);
  await latest(page, scored({ rqs: 70, rqsLabel: "STRONG", rqsTag: "NO PASS", passElDeg: null, passCorr: null }));
  const btn = page.locator("#rqs-why-btn"), why = page.locator("#rqs-why");
  await expect(why).toBeHidden();
  await btn.click();
  await expect(why).toBeVisible();
  await expect(btn).toHaveAttribute("aria-expanded", "true");
  await expect(why).toContainText("can score at most 70");
  await expect(why).toContainText("box air at the BMP388 board");
  await expect(why).toContainText("compensation off (not measured yet)");
  await latest(page, scored({ rqsTempC: null }));
  await expect(why).toContainText("TEMP N/A");
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
  await page.keyboard.press("Escape");
  await expect(why).toBeHidden();
  await expect(btn).toHaveAttribute("aria-expanded", "false");
});

test("RQS sparkline, radio overlays, score chart and detection log", async ({ page }, info) => {
  await open(page);
  await expect(page.locator("#rqs-spark-line")).toHaveAttribute("d", /^M[\d.]+ [\d.]+ L/);
  await page.locator('[data-range="24h"]').click();
  await page.locator("#tab-radio").click();
  const rf = await page.waitForFunction(() => {
    const c = window.Chart?.getChart(document.querySelector('[data-chart="rf"] canvas'));
    const d = c?.data.datasets;
    return d && d[2].data.length > 100 && c.$spans
      ? { sets: d.length, base: d[2].data.filter((p) => p.y !== null).length, thr: d[3].data.length,
          pass: c.$spans.pass.length, det: c.$spans.det.length, labels: d.map((x) => x.label) }
      : null;
  });
  const r = await rf.jsonValue();
  expect(r.base).toBeGreaterThan(100);
  expect(r.thr).toBeGreaterThan(100);
  expect(r.pass).toBeGreaterThanOrEqual(3);   // the made-up passes in the last 24 h
  expect(r.det).toBeGreaterThanOrEqual(3);    // three heard passes and one interference burst
  expect(r.labels).toEqual(["Raw", "Filtered", "Baseline", "Threshold", "Pass window", "Detection"]);
  const q = await page.waitForFunction(() => {
    const c = window.Chart?.getChart(document.querySelector('[data-chart="rqs"] canvas'));
    return c && c.data.datasets[0].data.length > 100 ? { max: c.options.scales.y.max, min: c.options.scales.y.min } : null;
  });
  expect(await q.jsonValue()).toEqual({ max: 100, min: 0 });
  const rows = page.locator("#det-rows tr");
  await expect(rows.first()).not.toContainText("No detections");
  expect(await rows.count()).toBeGreaterThanOrEqual(3);
  await expect(page.locator("#det-rows")).toContainText("yes");
  await expect(page.locator("#det-rows")).toContainText("no");   // the burst outside any pass
  await page.locator('[data-range="15m"]').click();
  await expect(page.locator("#det-rows")).toContainText(/ongoing|yes/);   // the pass in progress
  expect(info.consoleProblems).toEqual([]);
});

test("hover shows every series at the cursor's time", async ({ page }) => {
  await open(page);
  await page.locator("#tab-radio").click();
  const canvas = page.locator('[data-chart="rf"] canvas');
  await expect(canvas).toBeVisible();
  await page.waitForFunction(() => window.Chart?.getChart(document.querySelector('[data-chart="rf"] canvas'))?.data.datasets[0].data.length > 50);
  const box = await canvas.boundingBox();
  await page.mouse.move(box.x + box.width * 0.6, box.y + box.height / 2);
  const active = await page.evaluate(() => {
    const c = window.Chart.getChart(document.querySelector('[data-chart="rf"] canvas'));
    return c.tooltip.getActiveElements().map((a) => a.datasetIndex);
  });
  // Raw, filtered, baseline and threshold together, not hopping between them.
  expect(active.sort()).toEqual([0, 1, 2, 3]);
});
