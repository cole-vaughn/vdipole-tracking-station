// Unit tests for site/js/lib/rqs.js (reception quality score on the page, SPEC 14.14). Every record here is made up.
import { test } from "node:test";
import assert from "node:assert/strict";
import { bandFor, rqsView, rfLevel, whyLines, detectionLog, spans, sparkPoints } from "../../site/js/lib/rqs.js";

const rec = (over = {}) => ({
  ts: 1000, rqs: 57.3, rqsLabel: "GOOD", rqsTag: "PASS", rfExcessDb: 6.2, rfBaselineDbm: -75.1, rfThresholdDbm: -73.9,
  rfSigmaDb: 0.3, rfDetected: true, passElDeg: 41.6, passCorr: 0.83, nextPassEpoch: 1800000, rqsTempC: 23.4,
  rqsSnr: 0.52, rqsDet: 1, rqsPass: 0.93, rqsHealth: 1, battSocPct: 70, ...over,
});

test("rqs: bands match the label lines (15, 40, 70)", () => {
  assert.equal(bandFor(0), "nosignal");
  assert.equal(bandFor(14.9), "nosignal");
  assert.equal(bandFor(15), "weak");
  assert.equal(bandFor(39.9), "weak");
  assert.equal(bandFor(40), "good");
  assert.equal(bandFor(69.9), "good");
  assert.equal(bandFor(70), "strong");
  assert.equal(bandFor(null), "none");
});

test("rqs: view for every state", () => {
  const none = rqsView(null);
  assert.equal(none.state, "none");
  assert.equal(none.text, "--");
  assert.ok(none.bars.every((b) => b.value === null));

  const old = rqsView({ ts: 1, rfFiltDbm: -75 });            // a record from firmware before 1.2
  assert.equal(old.state, "none");

  const warm = rqsView(rec({ rqs: null, rqsLabel: "WARMING UP", rqsSnr: null, rqsDet: null, rqsPass: null, rqsHealth: null }));
  assert.deepEqual([warm.state, warm.text, warm.label, warm.band], ["warming", "--", "WARMING UP", "none"]);
  assert.ok(warm.bars.every((b) => b.value === null));

  const good = rqsView(rec());
  assert.deepEqual([good.state, good.text, good.label, good.band, good.tag], ["scored", "57", "GOOD", "good", "PASS"]);
  assert.deepEqual(good.bars.map((b) => b.value), [0.52, 1, 0.93, 1]);
  assert.equal(good.nextPassMs, 1800000 * 1000);
  assert.match(good.aria, /57 of 100, GOOD, in a predicted pass/);
  assert.equal(good.tempNa, false);
  assert.equal(good.battNa, false);

  assert.equal(rqsView(rec({ rqs: 3.2, rqsLabel: "NO SIGNAL", rqsTag: "NO PASS" })).label, "NO SIGNAL");
  assert.equal(rqsView(rec({ rqs: 22, rqsLabel: "WEAK" })).band, "weak");
  assert.equal(rqsView(rec({ rqs: 70, rqsLabel: "STRONG", rqsTag: "NO PASS" })).band, "strong");
  assert.equal(rqsView(rec({ rqsTag: "NO SCHEDULE" })).tagWords, "no pass schedule");
  assert.equal(rqsView(rec({ rqsTag: "TEST" })).tagWords, "test mode");

  const flags = rqsView(rec({ rqsTempC: null, battSocPct: null }));
  assert.equal(flags.tempNa, true);
  assert.equal(flags.battNa, true);

  const off = rqsView(rec(), { stationOffline: true });
  assert.equal(off.band, "none", "grey while offline");
  assert.equal(off.text, "57", "the last value stays readable");
});

test("rqs: S-meter level from the station's excess", () => {
  assert.equal(rfLevel(rec({ rfExcessDb: 6 })), 0.5);
  assert.equal(rfLevel(rec({ rfExcessDb: 30 })), 1);
  assert.equal(rfLevel(rec({ rfExcessDb: -2 })), 0);
  assert.equal(rfLevel(rec({ rfExcessDb: null })), 0);
  assert.equal(rfLevel(null), 0);
});

test("rqs: why this score, in plain words", () => {
  const rows = Object.fromEntries(whyLines(rec(), { rqsAlphaT: 0 }));
  assert.match(rows.Strength, /\+6\.2 dB above the noise baseline \(-75\.1 dBm/);
  assert.match(rows.Detection, /^On\. .*4 times the noise spread/);
  assert.match(rows["Pass match"], /42° elevation.*correlation 0\.83/);
  assert.match(rows["Pass match"], /only counts while a signal is detected/);
  assert.match(rows.Temperature, /23\.4 °C.*box air at the BMP388 board, not the RF detector chip.*compensation off \(not measured yet\)/i);
  assert.match(Object.fromEntries(whyLines(rec(), { rqsAlphaT: 0.021 })).Temperature, /compensation on, 0\.021 dB per °C/i);
  assert.match(Object.fromEntries(whyLines(rec(), null)).Temperature, /compensation unknown/i);
  assert.match(Object.fromEntries(whyLines(rec(), null, "F")).Temperature, /74\.1 °F/);

  assert.match(Object.fromEntries(whyLines(rec({ rqsTag: "NO PASS" }), {}))["Pass match"], /at most 70.*interference/);
  assert.match(Object.fromEntries(whyLines(rec({ rqsTag: "NO SCHEDULE" }), {}))["Pass match"], /most it can score is 85/);
  assert.match(Object.fromEntries(whyLines(rec({ rqsTag: "TEST" }), {}))["Pass match"], /Test mode is on/);
  assert.match(Object.fromEntries(whyLines(rec({ rqsTempC: null }), {})).Temperature, /^TEMP N\/A/);
  assert.match(Object.fromEntries(whyLines(rec({ battSocPct: null }), {})).Health, /BATT N\/A/);
  const warm = Object.fromEntries(whyLines(rec({ rqs: null, rqsLabel: "WARMING UP" }), {}));
  assert.match(warm["Warming up"], /5 minutes/);
  assert.equal(warm.Strength, undefined);
  assert.match(whyLines(null, null)[0][1], /firmware 1\.2/);
});

test("rqs: detection log from history", () => {
  const h = (ts, det, extra = {}) => ({ ts, rfDetected: det, rqsTag: "NO PASS", ...extra });
  const recs = [
    h(0, false), h(10e3, true, { rfExcessDb: 3, rqs: 40 }), h(20e3, true, { rfExcessDb: 9.5, rqs: 66, rqsTag: "PASS" }),
    h(30e3, true, { rfExcessDb: 7, rqs: 60 }), h(40e3, false),
    h(50e3, true, { rfExcessDb: 2 }), h(200e3, true, { rfExcessDb: 4 }),   // a 150 s gap splits these two
  ];
  const log = detectionLog(recs, 30e3);
  assert.equal(log.length, 3);
  const first = log[2];
  assert.deepEqual([first.startTs, first.durationS, first.peakExcess, first.peakRqs, first.inPass], [10e3, 20, 9.5, 66, true]);
  assert.equal(log[1].durationS, 0);
  assert.equal(log[0].ongoing, true, "the newest one is still on");
  assert.equal(detectionLog([]).length, 0);
});

test("rqs: shading spans and the sparkline", () => {
  const recs = [0, 10, 20, 30, 40, 50].map((s, i) => ({ ts: s * 1000, passElDeg: i >= 2 && i <= 3 ? 20 : null, rqs: i * 10 }));
  assert.deepEqual(spans(recs, (r) => r.passElDeg !== null), [[20e3, 40e3]]);
  assert.deepEqual(spans([...recs, { ts: 100e3, passElDeg: 9 }], (r) => r.passElDeg !== null, 30e3), [[20e3, 40e3], [100e3, 100e3]]);
  const pts = sparkPoints([...recs, { ts: 120e3, rqs: 5 }], 120e3, 100e3, 30e3);
  assert.deepEqual(pts.map((p) => p.y), [20, 30, 40, 50, null, 5], "only the window, a gap where the station went quiet");
});
