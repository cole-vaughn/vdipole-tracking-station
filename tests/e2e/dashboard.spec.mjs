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

// ---------- Battery health (SPEC 14.14) ----------
const health = (page, h) => page.evaluate((x) => window.__fixture.setHealth(x), h);

test("battery health pill: estimate, low, learning, no data, offline", async ({ page }) => {
  await open(page);
  const pill = page.locator("#soh-pill");
  await expect(pill).toHaveAttribute("data-band", "good");
  await expect(page.locator("#soh-text")).toHaveText("SOH 93% ±4");
  await expect(page.locator("#soh-est")).toBeVisible();

  await health(page, { battSohConf: "low", battSohPct: 71.4, battSohSigmaPct: 11.6 });
  await expect(page.locator("#soh-text")).toHaveText("SOH 71% ±12");
  await expect(pill).toHaveAttribute("data-band", "fair");
  await expect(pill).toHaveAttribute("data-conf", "low");
  await expect(pill).toHaveAttribute("aria-label", /fair, less runtime/);

  await health(page, { battSohConf: "high", battSohPct: 52, battSohSigmaPct: 4 });
  await expect(pill).toHaveAttribute("data-band", "replace");

  await health(page, { battSohConf: "none", battSohPct: null, battSohSigmaPct: null, battSohProgressPct: 42 });
  await expect(page.locator("#soh-text")).toHaveText("SOH learning 42%");
  await expect(page.locator("#soh-est")).toBeHidden();
  await expect(pill).toHaveAttribute("data-band", "none");

  await health(page, null);
  await expect(page.locator("#soh-text")).toHaveText("SOH --");

  await health(page, { battSohConf: "high", battSohPct: 93, battSohSigmaPct: 4 });
  await expect(pill).toHaveAttribute("data-band", "good");
  await latest(page, {}, { ageMs: 45_000 });
  await expect(page.locator("body")).toHaveAttribute("data-station", "offline", { timeout: 1500 });
  await expect(pill).toHaveAttribute("data-band", "none");
  await expect(page.locator("#soh-text")).toHaveText("SOH 93% ±4");
});

test("battery health pill with no health data at all", async ({ page }) => {
  await open(page, "?autoplay=0&health=none");
  await expect(page.locator("#soh-text")).toHaveText("SOH --");
  await page.locator("#soh-pill").click();
  await expect(page.locator("#soh-rows")).toContainText("no data from the station yet");
});

test("battery health panel opens and closes", async ({ page }, info) => {
  await open(page);
  const pill = page.locator("#soh-pill"), panel = page.locator("#soh-panel");
  await expect(panel).toBeHidden();
  await pill.click();
  await expect(panel).toBeVisible();
  await expect(pill).toHaveAttribute("aria-expanded", "true");
  await expect(panel).toContainText("not a lab capacity test");
  await expect(panel).toContainText("Uncalibrated");
  await expect(page.locator("#soh-rows")).toContainText("Wh of 26.6 Wh rated");
  await expect(page.locator("#soh-rows")).toContainText("Last discharge");
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);

  await page.keyboard.press("Escape");
  await expect(panel).toBeHidden();
  await expect(pill).toHaveAttribute("aria-expanded", "false");

  await pill.click();
  await expect(panel).toBeVisible();
  await page.locator("#fan-h").click();          // anywhere outside
  await expect(panel).toBeHidden();

  if (info.project.name === "desktop") {
    await pill.hover();
    await expect(panel).toBeVisible();
    await page.mouse.move(5, 5);
    await expect(panel).toBeHidden();
  }
});

test("battery health chart: all time, a break after a reset, discharges not used", async ({ page }, info) => {
  await open(page);
  await page.locator("#tab-health").click();
  await expect(page.locator("#range-note")).toBeVisible();
  await expect(page.locator('[data-range="1h"]')).toBeDisabled();
  const sets = await page.waitForFunction(() => {
    const c = window.Chart?.getChart(document.querySelector('[data-chart="soh"] canvas'));
    const d = c?.data.datasets;
    return d && d[2].data.length > 5
      ? { fitNulls: d[2].data.filter((p) => p.y === null).length, full: d[3].data.length, rest: d[4].data.length, eol: d[5].data[0].y }
      : null;
  });
  const s = await sets.jsonValue();
  expect(s.fitNulls).toBe(1);                    // the line breaks once, at the z reset
  expect(s.full).toBe(6);
  expect(s.rest).toBe(3);
  expect(s.eol).toBe(80);
  await expect(page.locator("#health-not-used li")).toHaveCount(4);
  await expect(page.locator("#health-not-used")).toContainText("too cold");
  await expect(page.locator("#health-not-used")).toContainText("learning reset (z)");

  // A new record arrives live and shows up without a reload.
  await page.evaluate(() => window.__fixture.addHealthLog({
    ts: Date.now(), ok: true, start: "full", e: 20.1, sliceSohPct: 92.7, fitSohPct: 93.0, fitSigmaPct: 4.2, hours: 17,
  }));
  await page.waitForFunction(() =>
    window.Chart.getChart(document.querySelector('[data-chart="soh"] canvas')).data.datasets[3].data.length === 7);

  await page.locator("#tab-env").click();
  await expect(page.locator('[data-range="1h"]')).toBeEnabled();
  await expect(page.locator("#range-note")).toBeHidden();
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
  expect(active.sort()).toEqual([0, 1]); // raw and filtered together, not hopping between them
});
