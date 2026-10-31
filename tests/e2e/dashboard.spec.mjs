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
  await expect(page.locator("#status")).toHaveAttribute("data-state", "live");
  await expect(page.locator("h1")).toHaveText("Satellite V-Dipole Tracking Station");
  // Give the lazy globe and the orbit module time to start, then check the console.
  await page.waitForTimeout(6000);
  expect(info.consoleProblems).toEqual([]);
});

test("old data shows OFFLINE banner, fresh data clears it", async ({ page }) => {
  await open(page);
  await latest(page, {}, { ageMs: 45_000 });
  await expect(page.locator("#status")).toHaveAttribute("data-state", "offline", { timeout: 1500 });
  await expect(page.locator("#banner")).toBeVisible();
  await expect(page.locator("#banner")).toContainText("Station offline");
  await latest(page, {}, { ageMs: 10_000 });
  await expect(page.locator("#status")).toHaveAttribute("data-state", "delayed");
  await latest(page, {});
  await expect(page.locator("#status")).toHaveAttribute("data-state", "live");
  await expect(page.locator("#banner")).toBeHidden();
});

test("age counter ticks without new data and goes OFFLINE by itself", async ({ page }) => {
  await page.clock.install(); // before load, so the page's 1 s ticker runs on the controlled clock
  await open(page);
  await latest(page, {}, { ageMs: 28_000 });
  await expect(page.locator("#status")).toHaveAttribute("data-state", "delayed");
  await page.clock.runFor(4000);
  await expect(page.locator("#status")).toHaveAttribute("data-state", "offline");
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
});
