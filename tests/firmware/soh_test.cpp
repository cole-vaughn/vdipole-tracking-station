// PC tests for firmware/station_firmware/soh.h (battery state of health, SPEC.md section 14).
// Every discharge curve here is SYNTHETIC: made by the simulated pack in pack_sim.h, never measured.
// Run with `npm run test:firmware`. Add `-v` for the accuracy table and per-test details.

#include <stdio.h>
#include <string.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../../firmware/station_firmware/soh.h"
#include "pack_sim.h"

using namespace soh;

static int g_checks = 0, g_failures = 0;
static bool g_verbose = false;
static const char* g_test = "";

#define CHECK(cond, ...)                                                        \
  do {                                                                          \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
      g_failures++;                                                             \
      printf("FAIL [%s] %s:%d: %s\n      ", g_test, __FILE__, __LINE__, #cond); \
      printf(__VA_ARGS__);                                                      \
      printf("\n");                                                             \
    }                                                                           \
  } while (0)

#define NOTE(...) do { if (g_verbose) { printf("      "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------- The station on the bench: simulated pack, estimator, and a pretend flash ----------
struct Rig {
  PackSim pack;
  Estimator est;
  std::vector<Result> log;                               // what would reach /station/healthLog
  std::map<std::string, std::vector<uint8_t>> flash;     // what would be in Preferences
  bool on = true;

  void drain() {
    uint8_t f = est.takeSaves();
    uint8_t buf[kBlobMax];
    if (f & REMOVE_OPEN) flash.erase("sohOpen");
    if (f & SAVE_SLICES) flash["sohSlices"].assign(buf, buf + est.saveSlices(buf, sizeof buf));
    if (f & SAVE_STATE)  flash["sohState"].assign(buf, buf + est.saveState(buf, sizeof buf));
    if (f & SAVE_OPEN)   flash["sohOpen"].assign(buf, buf + est.saveOpen(buf, sizeof buf));
    if (est.resultPending) {
      log.push_back(est.result);
      est.markPosted();
      drain();
    }
  }

  void feed(const Sample& s) {
    if (!on) return;
    est.step(s);
    drain();
  }

  void tick(double dt = 2) {
    Sample s = pack.step(dt);
    if (pack.dead) on = false;       // the pack's protection cut the power: the firmware stops
    feed(s);
  }

  void runHours(double h, double dt = 2) {
    for (long i = 0, n = (long)(h * 3600 / dt); i < n; i++) tick(dt);
  }

  bool runUntil(const std::function<bool()>& done, double maxHours) {
    for (long i = 0, n = (long)(maxHours * 1800); i < n; i++) {
      if (done()) return true;
      tick();
    }
    return done();
  }

  // Plug in, wait for the charger to finish, leave it 15 more minutes, unplug.
  void charge() {
    pack.plugged = true;
    pack.chargerDone = false;
    runUntil([&] { return pack.chargerDone; }, 10);
    runHours(0.25);
    pack.plugged = false;
  }

  // Power off for a while (the pack rests), then start again from what is in flash.
  void restart(double offMinutes = 1) {
    on = false;
    pack.stationOn = false;
    runHours(offMinutes / 60);
    pack.stationOn = true;
    on = !pack.dead;
    est = Estimator();
    auto load = [&](const char* key, bool (Estimator::*fn)(const uint8_t*, size_t)) {
      auto it = flash.find(key);
      if (it != flash.end()) (est.*fn)(it->second.data(), it->second.size());
    };
    load("sohSlices", &Estimator::loadSlices);
    load("sohState", &Estimator::loadState);
    load("sohOpen", &Estimator::loadOpen);
    drain();
  }

  double hoursSinceStart() const { return (est.tot.t - est.start.at.t) / 3600; }
};

static bool near(double a, double b, double tol) { return fabs(a - b) <= tol; }

// A hand-made slice for the fit tests: x SOE points at a given SOH, with the usual uncertainties.
static Slice makeSlice(double sohPct, double x, double sX = 2.0) {
  Slice s;
  s.x = (float)x;
  s.e = (float)(sohPct / 100 * SOH_RATED_WH * x / 100);
  s.sE = (float)(s.e * SOH_E_SIGMA_PCT / 100 + SOH_E_SIGMA_WH);
  s.sX = (float)sX;
  s.start = ANCHOR_FULL;
  return s;
}

// ===================================================================
// Tests
// ===================================================================
static void testTable() {
  CHECK(near(soeFromCellV(4.20f), 100, 1e-3), "%f", soeFromCellV(4.20f));
  CHECK(near(soeFromCellV(4.30f), 100, 1e-3), "%f", soeFromCellV(4.30f));
  CHECK(near(soeFromCellV(3.00f), 0, 1e-3), "%f", soeFromCellV(3.00f));
  CHECK(near(soeFromCellV(2.50f), 0, 1e-3), "%f", soeFromCellV(2.50f));
  CHECK(isnan(soeFromCellV(NAN)), "NaN in, NaN out");
  bool rising = true;
  for (float v = 2.95f; v < 4.25f; v += 0.001f) if (soeFromCellV(v + 0.001f) < soeFromCellV(v)) rising = false;
  CHECK(rising, "SOE must never fall as the voltage rises");
  // At half charge, less than half the energy is above empty (the lower half has the lower voltage).
  float mid = soeFromCellV(kCellV[10]);
  CHECK(mid > 45 && mid < 50, "SOE at 50%% charge = %.2f", mid);
  CHECK(near(socFromCellV(kCellV[10]), 50, 1e-3), "%f", socFromCellV(kCellV[10]));
}

static void testFullDischarge() {
  Rig r;
  r.pack.init(94, 0.30);
  r.runHours(0.5);                 // boot, too short to learn anything
  r.charge();
  r.runHours(0.25);
  CHECK(r.est.hasStart && r.est.start.kind == ANCHOR_FULL, "a full charge gives a full start anchor");
  CHECK(r.est.status().learn == LEARN_DISCHARGING, "learner %s", learnText(r.est.status().learn));

  r.runHours(6);
  Status mid = r.est.status();
  CHECK(mid.conf != CONF_NONE && r.est.liveHasOpen, "the open slice shows a number before the discharge ends");
  NOTE("after 6 h: SOH %.1f +-%.1f (%s)", mid.sohPct, mid.sigmaPct, confText(mid.conf));

  bool empty = r.runUntil([&] { return r.est.phase == PHASE_IDLE || !r.on; }, 30);
  CHECK(empty && r.est.phase == PHASE_IDLE, "the cutoff anchor ends the stretch (phase %d)", r.est.phase);
  CHECK(r.log.size() == 1, "one record, got %zu", r.log.size());
  if (r.log.empty()) return;
  const Result& x = r.log.back();
  Status st = r.est.status();
  CHECK(x.ok && x.start == ANCHOR_FULL, "ok %d start %d why %s", x.ok, x.start, whyText(x.why));
  CHECK(st.conf == CONF_HIGH, "conf %s, +-%.1f", confText(st.conf), st.sigmaPct);
  CHECK(fabs(st.sohPct - 94) <= st.sigmaPct, "SOH %.1f +-%.1f, true 94", st.sohPct, st.sigmaPct);
  CHECK(fabs(st.sohPct - 94) <= 4, "SOH %.1f, true 94", st.sohPct);
  CHECK(st.learn == LEARN_WAITING, "after the cutoff the learner waits for a charge");
  CHECK(near(r.est.cycleWh, r.pack.energyOutWh, 1.0), "cycle energy %.2f Wh, true %.2f Wh", r.est.cycleWh,
        r.pack.energyOutWh);
  NOTE("full discharge: %.2f Wh over %.1f pts in %.1f h -> SOH %.1f +-%.1f (true 94), cycles %.2f", x.e, x.x,
       x.hours, st.sohPct, st.sigmaPct, st.cycles);
}

// Short slices share the same voltage-table error, so repeating them must not fake confidence.
// One deep discharge afterwards is what tightens the result.
static void testShortSlicesStayHonest() {
  Rig r;
  r.pack.init(94, 0.90);
  for (int k = 0; k < 6; k++) {
    r.charge();                    // plugging in closes the slice before it
    r.runHours(3.25);
  }
  r.charge();
  CHECK(r.log.size() == 6, "six records, got %zu", r.log.size());
  if (r.log.size() < 6) return;
  for (size_t i = 0; i < r.log.size(); i++) {
    CHECK(r.log[i].ok, "slice %zu: %s", i + 1, whyText(r.log[i].why));
    NOTE("slice %zu: %.2f Wh over %.1f +-%.1f pts -> slice %.1f, fit %.1f +-%.1f", i + 1, r.log[i].e, r.log[i].x,
         r.log[i].sx, r.log[i].sliceSohPct, r.log[i].fitSohPct, r.log[i].fitSigmaPct);
  }
  float s1 = r.log[0].fitSigmaPct, s6 = r.log[5].fitSigmaPct, e6 = r.log[5].fitSohPct - 94;
  CHECK(s1 > 6, "one short slice is rough: +-%.1f", s1);
  CHECK(s6 >= s1 - 0.5, "six alike are not trusted more than one: +-%.1f vs +-%.1f", s6, s1);
  CHECK(fabs(e6) <= 1.5 * s6, "error %.1f stays inside 1.5 x +-%.1f", e6, s6);

  r.runUntil([&] { return r.est.phase == PHASE_IDLE || !r.on; }, 30);
  CHECK(r.log.size() == 7 && r.log.back().ok, "the deep slice is accepted");
  Status st = r.est.status();
  CHECK(st.sigmaPct < s6 - 3 && st.conf == CONF_HIGH, "a deep discharge tightens it: +-%.1f (%s)", st.sigmaPct,
        confText(st.conf));
  CHECK(fabs(st.sohPct - 94) <= 2 * st.sigmaPct, "fit %.1f +-%.1f, true 94", st.sohPct, st.sigmaPct);
  NOTE("after the deep slice: %.1f +-%.1f", st.sohPct, st.sigmaPct);
}

static void expectRejected(const char* what, Rig& r, int why, int detail = -1) {
  CHECK(!r.log.empty(), "%s: a record", what);
  if (r.log.empty()) return;
  const Result& x = r.log.back();
  CHECK(!x.ok && x.why == why, "%s: ok %d why '%s', want '%s'", what, x.ok, whyText(x.why), whyText(why));
  if (detail >= 0) CHECK(x.detail == detail, "%s: detail '%s', want '%s'", what, checkText(x.detail), checkText(detail));
  char buf[400];
  CHECK(resultJson(x, buf, sizeof buf) > 0, "%s: JSON", what);
  NOTE("%s -> %s", what, buf);
  CHECK(r.est.nSlices == 0, "%s: nothing stored", what);
}

static void testRejections() {
  {
    Rig r;
    r.pack.init(94, 0.9);
    r.pack.tempC = 8;
    r.charge(); r.runHours(10); r.charge();
    expectRejected("too cold", r, WHY_TOO_COLD);
  }
  {
    Rig r;
    r.pack.init(94, 0.9);
    r.pack.tempC = 40;
    r.charge(); r.runHours(10); r.charge();
    expectRejected("too hot", r, WHY_TOO_HOT);
  }
  {
    Rig r;
    r.pack.init(94, 0.9);
    r.pack.tempC = NAN;                 // full start and cutoff end need no temperature, but the slice does
    r.charge();
    r.runUntil([&] { return r.est.phase == PHASE_IDLE || !r.on; }, 30);
    expectRejected("no temperature", r, WHY_NO_TEMP);
  }
  {
    Rig r;
    r.pack.init(94, 0.55);
    r.runHours(1.05);                   // settle, then a rest start point
    CHECK(r.est.hasStart && r.est.start.kind == ANCHOR_REST, "rest start after the settle time");
    r.runHours(0.33);
    r.charge();
    expectRejected("too small", r, WHY_TOO_SMALL);
  }
  {
    Rig r;
    r.pack.init(94, 0.6);
    for (long i = 0; i < 3 * 1800; i++) {      // the load jumps by a quarter every 30 s
      r.pack.loadW = (i / 15) % 2 ? 1.5 : 1.18;
      r.tick();
    }
    r.pack.loadW = 1.18;
    r.charge();
    expectRejected("unsteady current", r, WHY_NO_START, CHECK_UNSTEADY);
  }
  {
    Rig r;
    r.pack.init(94, 0.9);
    r.charge();
    for (long i = 0; i < 10 * 1800; i++) r.tick(i % 300 == 299 ? 40 : 2);   // 30 s lost every 10 min
    r.charge();
    expectRejected("gaps", r, WHY_GAPS);
  }
}

static void testChargerMidStretch() {
  Rig r;
  r.pack.init(94, 0.9);
  r.charge();
  r.runHours(6);
  double expected = r.hoursSinceStart();
  r.pack.plugged = true;
  r.pack.chargerDone = false;
  r.runHours(0.1);
  CHECK(r.est.phase == PHASE_CHARGING, "charging detected");
  CHECK(r.log.size() == 1 && r.log.back().ok, "one accepted slice");
  if (r.log.empty()) return;
  const Result& x = r.log.back();
  CHECK(x.hours <= expected && x.hours >= expected - 0.05, "closed at the last good end point: %.3f h, plug at %.3f h",
        x.hours, expected);
  CHECK(near(x.sliceSohPct, 94, 15), "slice says %.1f", x.sliceSohPct);
  CHECK(r.est.lastClosed == CLOSED_CHARGER, "closed by %s", closedText(r.est.lastClosed));
}

static void testRestartMidStretch() {
  Rig r;
  r.pack.init(94, 0.9);
  r.charge();
  r.runHours(6);
  double expected = r.hoursSinceStart();
  CHECK(r.flash.count("sohOpen") == 1, "the open slice is in flash");
  r.restart(1);
  CHECK(r.log.size() == 1 && r.log.back().ok, "the saved slice closes after the restart (%zu records)", r.log.size());
  if (r.log.empty()) return;
  double got = r.log.back().hours;
  CHECK(got <= expected && got >= expected - SOH_OPEN_SAVE_EVERY_S / 3600.0 - 0.01,
        "closed at the saved end point: %.3f h, restart at %.3f h", got, expected);
  CHECK(r.flash.count("sohOpen") == 0, "the open blob is removed");
  CHECK(r.est.nSlices == 1 && r.est.fit.valid, "the slice is stored");
  r.runHours(0.1);
  CHECK(r.est.phase == PHASE_DISCHARGE && !r.est.hasStart, "a new stretch begins");
  CHECK(r.est.status().conf != CONF_NONE, "the fit survives the restart");
}

static void testBatteryLost() {
  Rig r;
  r.pack.init(94, 0.9);
  r.charge();
  r.runHours(4);
  for (int i = 0; i < 60; i++) {           // 2 minutes of no battery reading (USB power, button off)
    Sample s = r.pack.step(2);
    s.battV = 0.3f; s.mA = 0; s.mW = 0;
    r.feed(s);
  }
  CHECK(r.log.size() == 1 && r.log.back().ok, "the slice closes when the battery reading is lost");
  CHECK(r.est.lastClosed == CLOSED_LOST && r.est.phase == PHASE_LOST, "closed by %s", closedText(r.est.lastClosed));
  r.runHours(0.1);
  CHECK(r.est.phase == PHASE_DISCHARGE, "a new stretch when the reading comes back");
}

static void testBootOnCharger() {
  Rig r;
  r.pack.init(94, 0.93);
  r.pack.plugged = true;                   // the station starts while the charger holds the pack
  r.tick();
  CHECK(r.est.phase == PHASE_CHARGING, "starts in charging (phase %d)", r.est.phase);
  r.runUntil([&] { return r.pack.chargerDone; }, 5);
  r.runHours(0.25);
  r.pack.plugged = false;
  CHECK(r.est.phase == PHASE_DISCHARGE && r.est.hasStart && r.est.start.kind == ANCHOR_FULL,
        "a full start anchor after the charger stops (phase %d, start %d)", r.est.phase, r.est.hasStart);
}

static void testShortChargeIsNotFull() {
  Rig r;
  r.pack.init(94, 0.4);
  r.pack.plugged = true;
  r.runHours(0.5);                         // half an hour of constant current: nowhere near full
  r.pack.plugged = false;
  r.runHours(0.1);
  CHECK(r.est.phase == PHASE_DISCHARGE && !r.est.hasStart, "no full anchor from a partial charge");
}

static void testOutliers() {
  Estimator e;
  Result r = Estimator::blankResult();
  e.addSlice(makeSlice(60, 50), r);        // a bad slice first, then good ones
  e.addSlice(makeSlice(94, 50), r);
  e.addSlice(makeSlice(93, 50), r);
  CHECK(r.ok && r.why == WHY_OLD_OUTLIER && e.nSlices == 2, "older outlier dropped: why '%s', %d slices",
        whyText(r.why), e.nSlices);
  for (int i = 0; i < 3; i++) e.addSlice(makeSlice(94 + (i % 2 ? 1 : -1), 50), r);
  e.addSlice(makeSlice(60, 50), r);
  CHECK(!r.ok && r.why == WHY_OUTLIER, "new outlier rejected: ok %d why '%s'", r.ok, whyText(r.why));
  CHECK(near(e.fit.sohPct, 94, 1.5), "fit %.1f", e.fit.sohPct);
  e.addSlice(makeSlice(94, 50), r);        // a normal slice ends the outlier run
  CHECK(r.ok && e.nStreak == 0, "streak cleared");

  // The pack really changed (say a cell failed): three low slices in a row agree with each other.
  for (int i = 0; i < 3; i++) e.addSlice(makeSlice(70, 50), r);
  CHECK(r.ok && r.why == WHY_PACK_CHANGED && e.nSlices == 3, "pack change accepted: why '%s', %d slices",
        whyText(r.why), e.nSlices);
  CHECK(near(e.fit.sohPct, 70, 1.5), "fit follows to %.1f", e.fit.sohPct);
}

static void testAging() {
  Estimator e;
  Result r = Estimator::blankResult();
  PackSim noise;
  int outliers = 0;
  double trueSoh[100];
  for (int k = 0; k < 100; k++) {
    trueSoh[k] = 94 - 0.2 * k;
    Slice s = makeSlice(trueSoh[k] * (1 + 0.02 * noise.gauss()), 60, 2.2);
    e.addSlice(s, r);
    if (!r.ok) outliers++;
  }
  double mean = 0;
  for (int k = 68; k < 100; k++) mean += trueSoh[k] / 32;
  CHECK(e.nSlices == SOH_MAX_SLICES, "keeps the last %d slices, has %d", SOH_MAX_SLICES, e.nSlices);
  CHECK(near(e.fit.sohPct, mean, 2), "fit %.1f follows the aging pack (last 32 average %.1f, now %.1f)",
        e.fit.sohPct, mean, trueSoh[99]);
  CHECK(outliers <= 1, "slow aging is not mistaken for outliers (%d)", outliers);
}

static void testFitBasics() {
  Fit none = fitSlices(nullptr, 0);
  CHECK(!none.valid && none.conf == CONF_NONE, "no slices, no fit");
  Slice one = makeSlice(90, 40, 2.0);
  Fit f = fitSlices(&one, 1);
  CHECK(near(f.sohPct, 90, 0.01), "one slice: %.3f", f.sohPct);
  // One slice: relative error = sqrt((sE/e)^2 + (sX/x)^2), then the floor in quadrature.
  double rel = sqrt(pow(one.sE / one.e, 2) + pow(one.sX / one.x, 2));
  double want = sqrt(pow(rel * 90, 2) + SOH_FLOOR_PCT * SOH_FLOOR_PCT);
  CHECK(near(f.sigmaPct, want, 0.05), "sigma %.3f, want %.3f", f.sigmaPct, want);
  // Two slices that disagree: the plus-or-minus widens.
  Slice two[2] = {makeSlice(80, 40, 1.0), makeSlice(100, 40, 1.0)};
  Fit g = fitSlices(two, 2);
  Slice same[2] = {makeSlice(90, 40, 1.0), makeSlice(90, 40, 1.0)};
  Fit h = fitSlices(same, 2);
  CHECK(g.sigmaPct > h.sigmaPct + 3, "disagreement widens: %.1f vs %.1f", g.sigmaPct, h.sigmaPct);
  CHECK(near(g.sohPct, 90, 1.5), "between them: %.1f", g.sohPct);
}

static void testPersistence() {
  Estimator a;
  Result r = Estimator::blankResult();
  for (int i = 0; i < 5; i++) a.addSlice(makeSlice(90 + i, 40 + 5 * i), r);
  a.cycleWh = 123.45;
  a.post(r);
  uint8_t sl[kBlobMax], st[kBlobMax];
  size_t nsl = a.saveSlices(sl, sizeof sl), nst = a.saveState(st, sizeof st);
  CHECK(nsl > 0 && nst > 0, "blobs written (%zu, %zu bytes)", nsl, nst);
  NOTE("blob sizes: slices %zu bytes (5 of %d), state %zu bytes", nsl, SOH_MAX_SLICES, nst);

  Estimator b;
  CHECK(b.loadSlices(sl, nsl) && b.loadState(st, nst), "round trip loads");
  CHECK(b.nSlices == 5 && b.fit.sohPct == a.fit.sohPct && b.fit.sigmaPct == a.fit.sigmaPct, "same fit");
  CHECK(b.cycleWh == 123.45 && b.resultPending && b.result.ok == r.ok, "same state");

  Estimator full;
  for (int i = 0; i < SOH_MAX_SLICES; i++) full.addSlice(makeSlice(90, 50), r);
  size_t nfull = full.saveSlices(sl, sizeof sl);
  CHECK(nfull > 0 && nfull <= kBlobMax, "32 slices fit the blob (%zu bytes)", nfull);
  NOTE("32 slices: %zu bytes", nfull);

  uint8_t bad[kBlobMax];
  memcpy(bad, sl, nfull);
  bad[10] ^= 0x40;
  Estimator c;
  CHECK(!c.loadSlices(bad, nfull) && c.nSlices == 0 && !c.fit.valid, "a corrupt blob resets cleanly");
  CHECK(!c.loadSlices(sl, nfull - 1), "a cut-short blob is refused");
  CHECK(!c.loadSlices(nullptr, 0), "a missing blob is refused");
  memcpy(bad, sl, nfull);
  bad[0] = 99;                              // a future version, with a valid CRC
  uint32_t crc = crc32(bad, nfull - 4);
  memcpy(bad + nfull - 4, &crc, 4);
  CHECK(!c.loadSlices(bad, nfull), "an unknown version is refused");
  CHECK(!c.loadState(sl, nfull), "a blob of the wrong kind is refused");
}

static void testResetAndJson() {
  Estimator e;
  char buf[400];
  int n = healthJson(e.status(), false, buf, sizeof buf);
  CHECK(n > 0, "health JSON");
  CHECK(strstr(buf, "\"battSohPct\":null") && strstr(buf, "\"battSohConf\":\"none\"") &&
        strstr(buf, "\"inaCal\":false") && strstr(buf, "\"ts\":{\".sv\":\"timestamp\"}"), "%s", buf);
  NOTE("%s", buf);

  Result r = Estimator::blankResult();
  for (int i = 0; i < 4; i++) e.addSlice(makeSlice(94, 80, 1.2), r);
  e.updateLive();
  n = healthJson(e.status(), true, buf, sizeof buf);
  CHECK(n > 0 && n < 260, "health record is small: %d bytes", n);
  CHECK(strstr(buf, "\"battSohConf\":\"high\"") && strstr(buf, "\"battSohSlices\":4") && strstr(buf, "\"inaCal\":true"),
        "%s", buf);
  NOTE("%s (%d bytes)", buf, n);
  CHECK(healthJson(e.status(), true, buf, 40) == -1, "too small a buffer is reported");

  n = resultJson(r, buf, sizeof buf);
  CHECK(n > 0 && n < 260 && strstr(buf, "\"ok\":true") && strstr(buf, "\"why\":null"), "%s", buf);
  NOTE("%s (%d bytes)", buf, n);

  e.cycleWh = 50;
  e.resetAll();
  CHECK(e.nSlices == 0 && !e.fit.valid && e.cycleWh == 0 && e.status().conf == CONF_NONE, "z clears everything");
  CHECK(e.resultPending && e.result.reset, "z logs a reset marker");
  uint8_t f = e.takeSaves();
  CHECK((f & SAVE_STATE) && (f & SAVE_SLICES) && (f & REMOVE_OPEN), "z saves the cleared state (%d)", f);
  n = resultJson(e.result, buf, sizeof buf);
  CHECK(strstr(buf, "\"why\":\"reset\"") && strstr(buf, "\"fitSohPct\":null") && strstr(buf, "\"ok\":false"), "%s", buf);
  NOTE("%s", buf);

  Result nr = Estimator::blankResult();
  nr.why = WHY_NO_END;
  nr.detail = CHECK_UNSTEADY;
  resultJson(nr, buf, sizeof buf);
  CHECK(strstr(buf, "\"why\":\"no end point: current not steady\""), "%s", buf);
}

static void testDebugLines() {
  Rig r;
  r.pack.init(94, 0.9);
  r.charge();
  r.runHours(3);
  int lines = 0;
  r.est.debugLines([](const char* line, void* ctx) {
    (*(int*)ctx)++;
    if (g_verbose) printf("      | %s\n", line);
  }, &lines);
  CHECK(lines >= 4, "debug report has %d lines", lines);
}

// The table in SPEC 14.8, from the simulated pack: one slice from full down to each level, then three alike.
static void testAccuracyTable() {
  const double levels[] = {91, 85, 70, 30, 10};
  double prev1 = 1e9;
  if (g_verbose) {
    printf("      SYNTHETIC pack at SOH 94; its curve is off the table by up to 8 mV per cell, its resistance by 0.05 ohm\n");
    printf("      down to | hours | one slice | its error | three alike | their error\n");
  }
  for (double level : levels) {
    Rig r;
    r.pack.init(94, 0.9);
    float one = NAN, three = NAN, err1 = NAN, err3 = NAN, hours = NAN;
    for (int k = 0; k < 3; k++) {
      r.charge();
      r.runUntil([&] { return r.pack.trueSoe() <= level || !r.on; }, 30);
      r.charge();
      if (r.log.empty() || !r.log.back().ok) break;
      const Result& x = r.log.back();
      if (k == 0) { one = x.fitSigmaPct; err1 = x.fitSohPct - 94; hours = x.hours; }
      if (k == 2) { three = x.fitSigmaPct; err3 = x.fitSohPct - 94; }
    }
    CHECK(isfinite(one) && isfinite(three), "down to %.0f%%: three accepted slices", level);
    CHECK(one < prev1, "deeper slices are better: +-%.1f after +-%.1f", one, prev1);
    CHECK(fabs(err1) <= 1.5 * one, "down to %.0f%%: one slice, error %.1f within 1.5 x +-%.1f", level, err1, one);
    CHECK(three >= one - 0.5, "down to %.0f%%: three alike are not trusted more than one (+-%.1f vs +-%.1f)", level,
          three, one);
    CHECK(fabs(err3) <= 1.5 * three, "down to %.0f%%: error %.1f within 1.5 x +-%.1f", level, err3, three);
    prev1 = one;
    if (g_verbose)
      printf("      %5.0f%%  | %5.1f | +-%-7.1f | %+-9.1f | +-%-9.1f | %+.1f\n", level, hours, one, err1, three, err3);
  }
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) g_verbose = true;
  struct { const char* name; void (*fn)(); } tests[] = {
    {"voltage table and SOE", testTable},
    {"fit basics", testFitBasics},
    {"full discharge recovers the true SOH", testFullDischarge},
    {"short slices do not fake confidence; a deep one helps", testShortSlicesStayHonest},
    {"rejections", testRejections},
    {"charger mid-stretch closes at the last good anchor", testChargerMidStretch},
    {"restart mid-stretch closes at the saved anchor", testRestartMidStretch},
    {"battery reading lost", testBatteryLost},
    {"start while on the charger", testBootOnCharger},
    {"a partial charge is not a full anchor", testShortChargeIsNotFull},
    {"outliers and a real pack change", testOutliers},
    {"aging pack", testAging},
    {"persistence and corrupt blobs", testPersistence},
    {"reset and JSON", testResetAndJson},
    {"debug report", testDebugLines},
    {"accuracy table (SPEC 14.8)", testAccuracyTable},
  };
  for (auto& t : tests) {
    g_test = t.name;
    int before = g_failures;
    t.fn();
    printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
  }
  printf("\n%d checks, %d failed. All curves are SYNTHETIC (tests/firmware/pack_sim.h).\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
