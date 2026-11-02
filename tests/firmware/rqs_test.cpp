// PC tests for firmware/station_firmware/rqs.h (reception quality score, SPEC.md section 14).
// Every RF reading, temperature and pass here is SYNTHETIC: made up by the code below, never measured.
// Run with `npm run test:firmware`. Add `-v` for details and the measured JSON size.

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "../../firmware/station_firmware/rqs.h"

using namespace rqs;

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

static bool near(double a, double b, double tol) { return fabs(a - b) <= tol; }

// ---------- SYNTHETIC station: a noise floor, an optional signal, and the firmware's own Kalman filter ----------

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  double uniform() {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return ((s >> 11) + 0.5) / 9007199254740992.0;
  }
  double gauss() { return sqrt(-2.0 * log(uniform())) * cos(2.0 * 3.14159265358979323846 * uniform()); }
};

// Same update as KalmanFilter1D in station_firmware.ino, with the same settings (rfKalman.init(-80, 0.01, 1.0)).
struct Kalman {
  double est = -80, p = 1, q = 0.01, r = 1.0;
  double update(double z) {
    p += q;
    double k = p / (p + r);
    est += k * (z - est);
    p *= 1 - k;
    return est;
  }
};

static const int64_t T0 = 1793500000;   // 2026-11-01 02:26:40 UTC, a made-up start time

struct Station {
  Rng rng;
  Kalman kal;
  double floorDbm = -75, noiseDb = 1.0;
  std::unique_ptr<Engine> eng{new Engine};
  int64_t now = T0;
  float tempC = 24.0f, socPct = 80.0f;
  bool clockSet = true;

  // AD8307 reading: total power of the noise and a signal (dBm), then the firmware's Kalman filter.
  double reading(double sigDbm) {
    double n = floorDbm + noiseDb * rng.gauss();
    double total = 10 * log10(pow(10, n / 10) + (isfinite(sigDbm) ? pow(10, sigDbm / 10) : 0));
    return kal.update(total);
  }
  const Result& tick(double sigDbm = NAN) {
    Input in;
    in.dtS = 2;
    in.rfDbm = (float)reading(sigDbm);
    in.tempC = tempC;
    in.socPct = socPct;
    in.nowUtc = clockSet ? now : 0;
    now += 2;
    return eng->step(in);
  }
  void settle(int samples = 450) { for (int i = 0; i < samples; i++) tick(); }
};

// SYNTHETIC pass: elevation 5 degrees at aos and los, peaking at maxEl halfway.
static double passEl(int64_t t, int64_t aos, int64_t los, double maxEl) {
  double u = (double)(t - aos) / (double)(los - aos);
  return 5 + (maxEl - 5) * sin(3.14159265358979323846 * u);
}

static std::string scheduleJson(int64_t generated, const std::vector<std::vector<double>>& passes, int step = 30) {
  // Each pass: {aos, los, maxEl}. Elevations every `step` seconds, the last point at los.
  std::string s = "{\"source\":\"SYNTHETIC test schedule\",\"generated\":" + std::to_string(generated) +
                  ",\"validUntil\":" + std::to_string(generated + 48 * 3600) + ",\"noradId\":59051,\"minEl\":5,\"step\":" +
                  std::to_string(step) + ",\"passes\":[";
  for (size_t i = 0; i < passes.size(); i++) {
    int64_t aos = (int64_t)passes[i][0], los = (int64_t)passes[i][1];
    char b[64];
    snprintf(b, sizeof b, "%s{\"aos\":%lld,\"los\":%lld,\"maxEl\":%.1f,\"el\":[", i ? "," : "", (long long)aos,
             (long long)los, passes[i][2]);
    s += b;
    for (int64_t t = aos;; t += step) {
      int64_t tt = t < los ? t : los;
      snprintf(b, sizeof b, "%s%.1f", t == aos ? "" : ",", passEl(tt, aos, los, passes[i][2]));
      s += b;
      if (tt == los) break;
    }
    s += "]}";
  }
  return s + "]}";
}

static bool loadSchedule(Engine& e, const std::string& js) {
  Schedule* s = new Schedule;
  bool ok = parseSchedule(js.c_str(), js.size(), *s);
  if (ok) e.setSchedule(*s);
  delete s;
  return ok;
}

// ---------- Tests ----------

static void testStatsAndGuard() {
  // 200 training values 0..199, then 15 guard values far above: the guard must not move the baseline or sigma.
  std::unique_ptr<Ring> ring(new Ring);
  std::vector<float> scratch(RQS_RING_N);
  for (int i = 0; i < 200; i++) ring->push((float)i);
  for (int i = 0; i < RQS_GUARD_N; i++) ring->push(1000.0f);
  Stats s = ring->stats(scratch.data());
  CHECK(s.ok && s.training == 200, "training cells %u", (unsigned)s.training);
  CHECK(near(s.baseline, 40, 1e-4), "20th percentile of 0..199 is rank round(0.2 x 199) = 40, got %.3f", s.baseline);
  // MAD of 0..199: the median is 99.5, deviations 0.5..99.5, their median 50.
  CHECK(near(s.sigma, 1.4826 * 50, 1e-3), "sigma %.4f", s.sigma);

  // Too few training cells: no stats.
  ring->clear();
  for (int i = 0; i < RQS_MIN_TRAIN_N + RQS_GUARD_N - 1; i++) ring->push(1.0f);
  CHECK(!ring->stats(scratch.data()).ok, "one short of the minimum");
  ring->push(1.0f);
  Stats f = ring->stats(scratch.data());
  CHECK(f.ok, "at the minimum");
  CHECK(near(f.sigma, RQS_SIGMA_MIN_DB, 1e-6), "a flat ring gets the sigma floor, got %.3f", f.sigma);

  // Order statistic and median on small arrays.
  float a[] = {5, 1, 4, 2, 3};
  CHECK(near(orderStat(a, 5, 20), 2, 1e-6), "20th percentile of 1..5 is rank 1 (value 2)");
  float b[] = {4, 1, 3, 2};
  CHECK(near(median(b, 4), 2.5, 1e-6), "even count median");

  // The ring keeps the newest RQS_RING_N, oldest first.
  ring->clear();
  for (int i = 0; i < RQS_RING_N + 10; i++) ring->push((float)i);
  CHECK(ring->size() == RQS_RING_N && near(ring->at(0), 10, 0) && near(ring->at(RQS_RING_N - 1), RQS_RING_N + 9, 0),
        "wraps: oldest %.0f newest %.0f", ring->at(0), ring->at(RQS_RING_N - 1));
}

static void testWarmUp() {
  Station st;
  // The ring holds k - 1 samples at step k, and needs RQS_MIN_TRAIN_N + RQS_GUARD_N.
  const int need = RQS_MIN_TRAIN_N + RQS_GUARD_N + 1;
  for (int i = 1; i < need; i++) {
    const Result& r = st.tick();
    if (r.scored) { CHECK(false, "scored too early at step %d", i); break; }
  }
  const Result& w = st.eng->result();
  CHECK(w.label == LABEL_WARMING_UP && !isfinite(w.rqs) && !isfinite(w.baselineDbm) && !w.detected && !isfinite(w.sSnr),
        "warm-up result is empty, label %s", labelText(w.label));
  const Result& r = st.tick();
  CHECK(r.scored && isfinite(r.rqs), "scored at step %d", need);
  CHECK(r.label == LABEL_NO_SIGNAL, "noise only: %s (%.1f)", labelText(r.label), r.rqs);
  NOTE("noise: baseline %.2f dBm, sigma %.2f dB, threshold %.2f dBm", r.baselineDbm, r.sigmaDb, r.thresholdDbm);
}

static void testDetectionAndHysteresis() {
  Station st;
  st.noiseDb = 0;   // a flat floor, so every level below is exact
  st.settle(600);
  for (int i = 0; i < 100; i++) st.tick();
  const Result base = st.eng->result();
  CHECK(base.scored && near(base.sigmaDb, RQS_SIGMA_MIN_DB, 1e-6), "flat floor, sigma floor");
  const float b = base.baselineDbm, s = base.sigmaDb;

  // Feed the engine directly with exact values (no Kalman), using a fresh engine trained on the flat floor.
  std::unique_ptr<Engine> e(new Engine);
  Input in;
  in.tempC = 24; in.socPct = 80; in.nowUtc = 0;
  auto feed = [&](float v) { in.rfDbm = v; return e->step(in); };
  for (int i = 0; i < 500; i++) feed(b);
  float above = b + (RQS_K + 0.5f) * s, between = b + (RQS_K - 0.5f) * s, below = b + (RQS_K - 1.5f) * s;
  feed(above); feed(above);
  CHECK(!feed(below).detected, "two above then one below: not detected");
  feed(above); feed(above);
  CHECK(feed(above).detected, "three in a row: detected");
  CHECK(e->result().frozen, "frozen while detected");
  for (int i = 0; i < 20; i++) feed(between);
  CHECK(e->result().detected, "between the two lines: stays detected (hysteresis)");
  feed(below); feed(below); feed(between);
  CHECK(e->result().detected, "two below then one between: still detected");
  feed(below); feed(below);
  CHECK(feed(below).detected == false, "three in a row below: cleared");
}

static void testFreeze() {
  std::unique_ptr<Engine> e(new Engine);
  Input in;
  in.tempC = 24; in.socPct = 80; in.nowUtc = 0;
  auto feed = [&](float v) { in.rfDbm = v; return e->step(in); };
  for (int i = 0; i < 500; i++) feed(-75.0f);
  // A 5 minute signal well above the threshold.
  uint32_t before = e->pushes();
  for (int i = 0; i < 150; i++) feed(-60.0f);
  const Result& during = e->result();
  CHECK(during.detected && during.frozen, "detected and frozen");
  CHECK(e->pushes() - before == RQS_N_ON - 1, "only the %d samples before detection entered the ring, got %u",
        RQS_N_ON - 1, (unsigned)(e->pushes() - before));
  CHECK(near(during.baselineDbm, -75.0, 1e-4), "the signal did not raise its own baseline: %.2f", during.baselineDbm);
  // Clear, then the ring stays frozen for RQS_UNFREEZE_S.
  int clearedAt = -1, unfrozenAt = -1;
  for (int i = 0; i < 100; i++) {
    const Result& r = feed(-75.0f);
    if (clearedAt < 0 && !r.detected) clearedAt = i;
    if (clearedAt >= 0 && unfrozenAt < 0 && !r.frozen) unfrozenAt = i;
  }
  CHECK(clearedAt == RQS_N_OFF - 1, "cleared after %d samples below, at %d", RQS_N_OFF, clearedAt);
  double secs = (unfrozenAt - clearedAt) * 2.0;
  CHECK(near(secs, RQS_UNFREEZE_S, 2.0), "unfrozen %.0f s after clearing", secs);
}

static void testFreezeLimit() {
  // A lasting step in the noise floor (say the LNA powered on): detection must not stay on forever.
  std::unique_ptr<Engine> e(new Engine);
  Input in;
  in.tempC = 24; in.socPct = 80; in.nowUtc = 0;
  auto feed = [&](float v) { in.rfDbm = v; return e->step(in); };
  for (int i = 0; i < 500; i++) feed(-75.0f);
  double frozeFor = 0, clearedAfter = -1;
  for (int i = 0; i < 1800; i++) {      // one hour at the new level
    const Result& r = feed(-65.0f);
    if (r.frozen) frozeFor = r.frozenS;
    if (i > 10 && !r.detected && clearedAfter < 0) clearedAfter = i * 2.0;
  }
  CHECK(near(frozeFor, RQS_MAX_FREEZE_S - 2, 2.1), "froze for at most the limit: %.0f s", frozeFor);
  CHECK(clearedAfter > RQS_MAX_FREEZE_S && clearedAfter < 1800, "cleared after %.0f s once the baseline caught up",
        clearedAfter);
  CHECK(near(e->result().baselineDbm, -65.0, 0.01), "baseline followed the new floor: %.2f", e->result().baselineDbm);
  NOTE("step of +10 dB: frozen %.0f s, cleared after %.0f s", frozeFor, clearedAfter);
}

static void testFriis() {
  CHECK(near(slantRangeKm(90), 820, 1e-6), "overhead range is the orbit height: %.3f", slantRangeKm(90));
  CHECK(near(slantRangeKm(0), sqrt(7191.0 * 7191.0 - 6371.0 * 6371.0), 1e-6), "horizon range %.1f", slantRangeKm(0));
  CHECK(near(slantRangeKm(5), 2825, 1), "5 degrees: %.1f km", slantRangeKm(5));
  CHECK(near(expectedRelDb(90), 0, 1e-9), "overhead is 0 dB");
  CHECK(near(expectedRelDb(5), -10.75, 0.01), "5 degrees: %.2f dB", expectedRelDb(5));
  CHECK(near(expectedRelDb(15), -8.05, 0.01), "15 degrees: %.2f dB", expectedRelDb(15));
  double prev = -100;
  bool rising = true;
  for (int el = 0; el <= 90; el++) { double e = expectedRelDb(el); if (e <= prev) rising = false; prev = e; }
  CHECK(rising, "expected power rises with elevation");
}

static void testPearson() {
  Pearson p;
  for (int i = 0; i < 20; i++) p.add(i, 3 * i + 1);
  CHECK(near(p.r(), 1, 1e-12), "a straight line: %.6f", p.r());
  p.clear();
  for (int i = 0; i < 20; i++) p.add(i, -2.0 * i);
  CHECK(near(p.r(), -1, 1e-12), "opposite: %.6f", p.r());
  p.clear();
  for (int i = 0; i < 20; i++) p.add(5, i);
  CHECK(!isfinite(p.r()), "a flat side has no correlation");
  p.clear();
  for (int i = 0; i < RQS_CORR_MIN_N - 1; i++) p.add(i, i);
  CHECK(!isfinite(p.r()), "fewer than %d pairs", RQS_CORR_MIN_N);
  // Against a two-pass textbook calculation.
  double x[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, y[] = {2, 4, 5, 4, 5, 7, 8, 9, 10, 12, 11, 15};
  p.clear();
  double mx = 0, my = 0;
  for (int i = 0; i < 12; i++) { p.add(x[i], y[i]); mx += x[i] / 12; my += y[i] / 12; }
  double sxy = 0, sxx = 0, syy = 0;
  for (int i = 0; i < 12; i++) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
  CHECK(near(p.r(), sxy / sqrt(sxx * syy), 1e-12), "matches the two-pass formula: %.6f", p.r());
}

static void testScoreLabelsAndHealth() {
  CHECK(near(score(1, 1, 1, 1), 100, 1e-4), "all parts full");
  CHECK(near(score(1, 1, 0, 1), 70, 1e-4), "outside a pass, the most is 70");
  CHECK(near(score(1, 1, RQS_PASS_UNKNOWN, 1), 85, 1e-4), "no schedule, the most is 85");
  CHECK(near(score(0.5f, 0, 0.5f, 0.8f), 100 * 0.8 * (0.25 + 0.15), 1e-4), "weights");
  CHECK(score(2, 2, 2, 2) == 100 && score(-1, 0, 0, 1) == 0, "clamped");
  CHECK(labelFor(14.99f) == LABEL_NO_SIGNAL && labelFor(15) == LABEL_WEAK, "15 edge");
  CHECK(labelFor(39.99f) == LABEL_WEAK && labelFor(40) == LABEL_GOOD, "40 edge");
  CHECK(labelFor(69.99f) == LABEL_GOOD && labelFor(70) == LABEL_STRONG, "70 edge");
  CHECK(labelFor(NAN) == LABEL_WARMING_UP, "no score");
  CHECK(near(passPart(TAG_PASS, NAN), 0.4, 1e-6) && near(passPart(TAG_PASS, 0.3), 0.4, 1e-6), "pass, shape unknown or weak");
  CHECK(near(passPart(TAG_PASS, 0.6), 0.7, 1e-6) && near(passPart(TAG_PASS, 0.95), 1.0, 1e-6), "pass, shape 0.5 and full");
  CHECK(passPart(TAG_NO_PASS, 1.0) == 0 && passPart(TAG_TEST, NAN) == 0.5f && passPart(TAG_NO_SCHEDULE, 1) == 0.5f, "tags");
  CHECK(hBatt(NAN) == 1 && hBatt(50) == 1 && hBatt(20) == 1, "battery factor 1");
  CHECK(near(hBatt(10), 0.75, 1e-6) && near(hBatt(0), 0.5, 1e-6) && near(hBatt(-5), 0.5, 1e-6), "battery factor low");
  CHECK(hTemp(NAN) == 1 && hTemp(45) == 1 && near(hTemp(52.5f), 0.85, 1e-6), "temperature factor");
  CHECK(near(hTemp(60), 0.7, 1e-6) && near(hTemp(75), 0.7, 1e-6), "hot box stays at 0.7");
  CHECK(!strcmp(labelText(LABEL_NO_SIGNAL), "NO SIGNAL") && !strcmp(tagText(TAG_NO_SCHEDULE), "NO SCHEDULE") &&
            strlen(tagShort(TAG_NO_SCHEDULE)) <= 8,
        "texts");
}

static void testTemperature() {
  TempFilter f;
  CHECK(!f.step(NAN, 2), "missing BMP388");
  CHECK(f.step(25, 2) && f.smooth == 25, "first good sample starts the filter");
  CHECK(!f.step(90, 2), "out of range");
  CHECK(!f.step(-30, 2), "out of range, cold");
  CHECK(!f.step(31, 2), "a jump of 6 C is rejected");
  CHECK(f.step(31.5f, 2), "the next sample near it is accepted");
  // 60 s time constant: a 4 C step reaches 63% in 60 s.
  TempFilter g;
  g.step(25, 2);
  for (int i = 0; i < 30; i++) g.step(29, 2);
  CHECK(near(g.smooth, 25 + 4 * (1 - exp(-1.0)), 0.01), "smoothed %.3f after 60 s", g.smooth);
  // After more than 60 s without a good sample, it starts over instead of reusing an old value.
  for (int i = 0; i < 31; i++) g.step(NAN, 2);
  CHECK(g.step(30, 2) && g.smooth == 30, "restarted at %.2f", g.smooth);

  // In the engine: BMP388 unavailable means no compensation, temperature factor 1, rqsTempC null.
  Station st;
  st.eng->alphaT = 0.05f;   // a made-up nonzero coefficient, to see compensation switch off
  st.tempC = 55;            // hot box
  st.settle();
  const Result& hot = st.tick();
  CHECK(hot.tempOk && near(hot.compDb, 0.05 * (55 - 25), 1e-3), "compensation on: %.3f dB", hot.compDb);
  CHECK(near(hot.hTemp, 1 - 0.3 * 10 / 15, 1e-4), "hot box factor %.3f", hot.hTemp);
  st.tempC = NAN;
  const Result& gone = st.tick();
  CHECK(!gone.tempOk && gone.compDb == 0 && gone.hTemp == 1 && !isfinite(gone.tempC), "BMP388 missing: comp %.2f, H %.2f",
        gone.compDb, gone.hTemp);
  char js[600];
  appendJson(gone, false, js, sizeof js);
  CHECK(strstr(js, "\"rqsTempC\":null") != nullptr, "rqsTempC is null: %s", js);
  st.tempC = 99;
  CHECK(!st.tick().tempOk, "a 99 C reading is rejected");
  st.socPct = NAN;
  const Result& nb = st.tick();
  CHECK(!nb.battKnown && nb.hBatt == 1, "unknown charge: battery factor 1");
}

static void testScheduleParser() {
  std::string good = scheduleJson(T0, {{T0 + 3600, T0 + 3600 + 700, 42.7}, {T0 + 9000, T0 + 9000 + 400, 12.0}});
  std::unique_ptr<Schedule> s(new Schedule);
  CHECK(parseSchedule(good.c_str(), good.size(), *s) && s->ok, "good file");
  CHECK(s->n == 2 && s->generated == T0 && s->validUntil == T0 + 48 * 3600 && s->stepS == 30, "header and count");
  CHECK(s->p[0].aos == T0 + 3600 && s->p[0].los == T0 + 4300 && near(s->p[0].maxEl, 42.7, 1e-4), "first pass");
  CHECK(s->p[0].nEl == 25, "700 s at 30 s steps: 24 steps plus the point at los, got %u", (unsigned)s->p[0].nEl);
  CHECK(near(s->elevationAt(s->p[0], T0 + 3600), 5.0, 0.05), "elevation at aos");
  CHECK(near(s->elevationAt(s->p[0], T0 + 3600 + 15), (passEl(T0 + 3600, T0 + 3600, T0 + 4300, 42.7) +
                                                       passEl(T0 + 3630, T0 + 3600, T0 + 4300, 42.7)) / 2, 0.06),
        "halfway between two points");
  CHECK(near(s->elevationAt(s->p[0], T0 + 4300), 5.0, 0.05), "elevation at los");

  // Whitespace, extra keys and nested objects are fine.
  std::string spaced = "{ \"x\" : {\"a\":[1,2,{\"b\":null}]}, \"generated\" : 100 , \"validUntil\":200,\n"
                       "  \"passes\" : [ { \"aos\":150, \"los\":160, \"maxEl\":7, \"el\":[ 5, 6 ], \"note\":\"hi\" } ] , \"ok\":true }";
  CHECK(parseSchedule(spaced.c_str(), spaced.size(), *s) && s->n == 1, "tolerant of layout");

  std::string cut = good.substr(0, good.size() / 2);
  CHECK(!parseSchedule(cut.c_str(), cut.size(), *s) && !s->ok, "cut short");
  const char* junk = "<html>404 Not Found</html>";
  CHECK(!parseSchedule(junk, strlen(junk), *s), "an error page");
  CHECK(!parseSchedule("", 0, *s), "empty");
  std::string trailing = good + "x";
  CHECK(!parseSchedule(trailing.c_str(), trailing.size(), *s), "trailing junk");
  std::string backwards = "{\"generated\":100,\"validUntil\":200,\"passes\":[{\"aos\":160,\"los\":150,\"el\":[5]}]}";
  CHECK(!parseSchedule(backwards.c_str(), backwards.size(), *s), "los before aos");
  std::string overlap = "{\"generated\":100,\"validUntil\":200,\"passes\":[{\"aos\":150,\"los\":170,\"el\":[5]},"
                        "{\"aos\":160,\"los\":180,\"el\":[5]}]}";
  CHECK(!parseSchedule(overlap.c_str(), overlap.size(), *s), "overlapping passes");
  std::string noValid = "{\"generated\":100,\"passes\":[]}";
  CHECK(!parseSchedule(noValid.c_str(), noValid.size(), *s), "no validUntil");
  std::string tooMany = "{\"generated\":100,\"validUntil\":200,\"passes\":[{\"aos\":150,\"los\":170,\"el\":[";
  for (int i = 0; i <= RQS_MAX_EL; i++) tooMany += (i ? ",5" : "5");
  tooMany += "]}]}";
  CHECK(!parseSchedule(tooMany.c_str(), tooMany.size(), *s), "more elevations than fit");
  std::vector<std::vector<double>> many;
  for (int i = 0; i < RQS_MAX_PASSES + 5; i++) many.push_back({(double)(T0 + 1000 + i * 2000), (double)(T0 + 1600 + i * 2000), 20});
  std::string lots = scheduleJson(T0, many);
  CHECK(parseSchedule(lots.c_str(), lots.size(), *s) && s->n == RQS_MAX_PASSES, "extra passes dropped, kept %u", (unsigned)s->n);
}

static void testTags() {
  Station st;
  st.settle();
  CHECK(st.tick().tag == TAG_NO_SCHEDULE && st.eng->result().sched == SCHED_NONE, "no schedule loaded");
  CHECK(loadSchedule(*st.eng, scheduleJson(st.now, {{(double)(st.now + 600), (double)(st.now + 1200), 40}})), "load");
  const Result& out = st.tick();
  CHECK(out.tag == TAG_NO_PASS && near(out.sPass, 0, 0) && out.nextPassUtc == st.now - 2 + 600, "valid schedule, outside");
  CHECK(!isfinite(out.passElDeg), "no elevation outside a pass");
  st.now += 600;
  const Result& in = st.tick();
  CHECK(in.tag == TAG_PASS && isfinite(in.passElDeg) && near(in.sPass, RQS_PASS_BASE, 1e-6), "inside: pass part %.2f", in.sPass);
  CHECK(in.nextPassUtc == 0, "no later pass in this schedule");
  st.eng->testMode = true;
  CHECK(st.tick().tag == TAG_TEST && near(st.eng->result().sPass, 0.5, 0), "test mode overrides");
  st.eng->testMode = false;
  st.clockSet = false;
  CHECK(st.tick().tag == TAG_NO_SCHEDULE && st.eng->result().sched == SCHED_NO_CLOCK, "clock not set");
  st.clockSet = true;
  st.now += 49 * 3600;
  CHECK(st.tick().tag == TAG_NO_SCHEDULE && st.eng->result().sched == SCHED_EXPIRED, "expired schedule");
}

// One SYNTHETIC pass, overhead power 20 dB above the floor, following the free-space curve.
struct PassRun {
  float peakRqs = 0, lastCorr = NAN, peakExcess = -99;
  bool detected = false;
  Label peakLabel = LABEL_WARMING_UP;
  Tag tagAtPeak = TAG_NO_SCHEDULE;
};

static PassRun runPass(Station& st, bool inSchedule, bool loadAny) {
  int64_t aos = st.now + 300, los = aos + 720;
  if (loadAny) {
    double sAos = inSchedule ? aos : aos + 6 * 3600;   // "outside": the schedule's pass is hours away
    loadSchedule(*st.eng, scheduleJson(st.now, {{sAos, sAos + 720, 60}}));
  }
  PassRun pr;
  while (st.now < los + 300) {
    double sig = (st.now >= aos && st.now <= los) ? -55 + expectedRelDb(passEl(st.now, aos, los, 60)) : NAN;
    const Result& r = st.tick(sig);
    if (r.detected) pr.detected = true;
    if (r.scored && r.rqs > pr.peakRqs) { pr.peakRqs = r.rqs; pr.peakLabel = r.label; pr.tagAtPeak = r.tag; }
    if (r.scored && r.excessDb > pr.peakExcess) pr.peakExcess = r.excessDb;
    if (isfinite(r.passCorr)) pr.lastCorr = r.passCorr;
  }
  return pr;
}

static void testFullPass() {
  Station st;
  st.settle();
  PassRun pr = runPass(st, true, true);
  CHECK(pr.detected, "the pass is detected");
  CHECK(pr.lastCorr > 0.9, "follows the free-space curve: r %.3f", pr.lastCorr);
  CHECK(pr.peakRqs >= 90 && pr.peakLabel == LABEL_STRONG && pr.tagAtPeak == TAG_PASS, "peak %.1f %s %s", pr.peakRqs,
        labelText(pr.peakLabel), tagText(pr.tagAtPeak));
  CHECK(!st.eng->result().detected && st.eng->result().label == LABEL_NO_SIGNAL, "back to NO SIGNAL afterwards: %s",
        labelText(st.eng->result().label));
  NOTE("pass: peak RQS %.1f, peak excess %.1f dB, final r %.3f", pr.peakRqs, pr.peakExcess, pr.lastCorr);

  // The same rise with no pass in the schedule: at most 70, tagged NO PASS.
  Station out;
  out.settle();
  PassRun po = runPass(out, false, true);
  CHECK(po.detected && po.peakRqs <= 70.0f + 1e-3 && po.tagAtPeak == TAG_NO_PASS, "outside a pass: %.1f %s", po.peakRqs,
        tagText(po.tagAtPeak));
  // And with no schedule at all: at most 85.
  Station none;
  none.settle();
  PassRun pn = runPass(none, false, false);
  CHECK(pn.peakRqs <= 85.0f + 1e-3 && pn.tagAtPeak == TAG_NO_SCHEDULE, "no schedule: %.1f %s", pn.peakRqs,
        tagText(pn.tagAtPeak));
  NOTE("same rise: outside a pass %.1f, no schedule %.1f", po.peakRqs, pn.peakRqs);

  // A rise that does not follow the curve (flat for the whole window) scores lower in the pass part.
  Station flat;
  flat.settle();
  int64_t aos = flat.now + 300, los = aos + 720;
  loadSchedule(*flat.eng, scheduleJson(flat.now, {{(double)aos, (double)los, 60}}));
  float lastPass = 0;
  while (flat.now < los) {
    const Result& r = flat.tick(flat.now >= aos ? -60 : NAN);
    if (r.tag == TAG_PASS && isfinite(r.sPass)) lastPass = r.sPass;
  }
  CHECK(lastPass < 0.75f, "a flat rise does not match the shape: pass part %.2f", lastPass);
}

static void testNoFalseAlarms() {
  Station st;
  int detections = 0;
  bool prev = false;
  float worst = 0;
  for (int i = 0; i < 6 * 1800; i++) {   // six hours of noise, no schedule
    const Result& r = st.tick();
    if (r.detected && !prev) detections++;
    prev = r.detected;
    if (r.scored && r.rqs > worst) worst = r.rqs;
  }
  CHECK(detections == 0, "six hours of noise: %d false detections", detections);
  CHECK(worst < RQS_LABEL_WEAK, "noise with no schedule stays NO SIGNAL: worst %.1f", worst);

  // Noise during a scheduled pass, and in test mode: still NO SIGNAL (the pass part needs a detection).
  Station p;
  p.settle();
  int64_t aos = p.now + 60, los = aos + 720;
  loadSchedule(*p.eng, scheduleJson(p.now, {{(double)aos, (double)los, 60}}));
  float worstPass = 0;
  while (p.now < los) { const Result& r = p.tick(); if (r.tag == TAG_PASS && r.rqs > worstPass) worstPass = r.rqs; }
  CHECK(worstPass > 0 && worstPass < RQS_LABEL_WEAK, "noise during a pass: worst %.1f", worstPass);
  p.eng->testMode = true;
  CHECK(p.tick().label == LABEL_NO_SIGNAL, "noise in test mode: %.1f", p.eng->result().rqs);
  NOTE("noise only: worst %.1f with no schedule, %.1f during a pass", worst, worstPass);
}

static void testJsonAndSize() {
  Station st;
  char js[700];
  int warm = appendJson(st.tick(), true, js, sizeof js);
  CHECK(warm > 0 && strstr(js, "\"rqs\":null") && strstr(js, "\"rqsLabel\":\"WARMING UP\"") &&
            strstr(js, "\"rfDetected\":false") && strstr(js, "\"rqsSnr\":null") && strstr(js, "\"nextPassEpoch\":null"),
        "warm-up JSON: %s", js);
  st.settle();
  loadSchedule(*st.eng, scheduleJson(st.now, {{(double)(st.now + 100), (double)(st.now + 800), 50}}));
  st.now += 300;
  for (int i = 0; i < 20; i++) st.tick(-58);
  const Result& r = st.eng->result();
  int hist = appendJson(r, false, js, sizeof js);
  std::string h = js;
  int latest = appendJson(r, true, js, sizeof js);
  CHECK(hist > 0 && latest > hist, "both fit");
  NOTE("%s", js);
  const char* keys[] = {"rqs", "rqsLabel", "rqsTag", "rfExcessDb", "rfBaselineDbm", "rfThresholdDbm", "rfSigmaDb",
                        "rfDetected", "passElDeg", "passCorr", "nextPassEpoch", "rqsTempC"};
  for (const char* k : keys) CHECK(h.find(std::string("\"") + k + "\":") != std::string::npos, "history has %s", k);
  CHECK(h.find("rqsSnr") == std::string::npos && strstr(js, "\"rqsHealth\":"), "parts in latest only");
  CHECK(strstr(js, "\"rqsTag\":\"PASS\"") && !strstr(js, "nan"), "in a pass, no NaN: %s", js);
  CHECK(appendJson(r, true, js, 50) == -1, "reports a buffer that is too small");
  // Measured size of the new fields, for SPEC 14.13 (the old record was 417 bytes).
  CHECK(hist <= 300 && latest - hist <= 100, "history fields %d bytes, latest-only parts %d bytes", hist, latest - hist);
  NOTE("measured: history adds %d bytes (record about %d), latest adds %d more (about %d)", hist, 417 + hist,
       latest - hist, 417 + latest);
}

static void testFormatAndReport() {
  char b[24];
  formatUtc(0, b, sizeof b);
  CHECK(!strcmp(b, "1970-01-01 00:00:00"), "%s", b);
  formatUtc(951782400, b, sizeof b);
  CHECK(!strcmp(b, "2000-02-29 00:00:00"), "leap day: %s", b);
  formatUtc(1793512345, b, sizeof b);
  CHECK(!strcmp(b, "2026-11-01 05:52:25"), "%s", b);

  Station st;
  st.settle();
  loadSchedule(*st.eng, scheduleJson(st.now, {{(double)(st.now + 600), (double)(st.now + 1200), 40}}));
  st.tick();
  std::vector<std::string> lines;
  st.eng->debugLines([](const char* l, void* ctx) { static_cast<std::vector<std::string>*>(ctx)->push_back(l); }, &lines,
                     st.now);
  std::string all;
  for (auto& l : lines) { all += l + "\n"; NOTE("%s", l.c_str()); }
  CHECK(all.find("Baseline") != std::string::npos && all.find("Schedule: ok, 1 passes") != std::string::npos &&
            all.find("pass 2026-11-01") != std::string::npos && all.find("Temperature (BMP388)") != std::string::npos,
        "report:\n%s", all.c_str());
}

// ---------- Ground mode and distance (SPEC 16) ----------

static void testRanger() {
  CHECK(near(signalPowerDbm(-50, -58), 10 * log10(1e-5 - pow(10, -5.8)), 1e-4), "floor taken away as power: %.3f",
        signalPowerDbm(-50, -58));
  CHECK(!isfinite(signalPowerDbm(-58, -58)) && !isfinite(signalPowerDbm(-60, -58)), "no signal above the floor");

  Ranger r;
  float d, lo, hi;
  CHECK(!r.fitted && !r.estimate(-40, d, lo, hi), "no points, no estimate");
  r.add(3, -40);   // one point: free-space exponent assumed
  CHECK(r.fitted && near(r.nExp, 2, 1e-6) && near(r.sigmaDb, RQS_DIST_SIGMA_DB, 1e-6), "one point: n %.2f", r.nExp);
  CHECK(r.estimate(-40 - 6.0206f, d, lo, hi) && near(d, 6, 0.01), "6 dB weaker is twice as far: %.3f m", d);

  // SYNTHETIC points from P1 = -30 dBm and n = 2.5, no noise.
  Ranger s;
  for (float m : {1.0f, 3.0f, 5.0f, 10.0f}) s.add(m, -30 - 25 * log10f(m));
  CHECK(near(s.nExp, 2.5, 1e-3) && near(s.p1m, -30, 1e-3), "fit n %.3f, P1 %.2f", s.nExp, s.p1m);
  CHECK(near(s.sigmaDb, RQS_DIST_SIGMA_MIN_DB, 1e-6), "a perfect fit still claims the minimum spread");
  CHECK(s.estimate(-30 - 25 * log10f(7), d, lo, hi) && near(d, 7, 0.01), "7 m back: %.3f", d);
  double f = pow(10.0, RQS_DIST_SIGMA_MIN_DB / 25.0);
  CHECK(near(lo, 7 / f, 0.01) && near(hi, 7 * f, 0.01), "range %.2f to %.2f", lo, hi);
  s.add(3.01f, -50);   // replaces the 3 m point
  CHECK(s.n == 4, "a point at the same distance replaces the old one");

  Ranger c;
  c.add(1, -50); c.add(5, -40);   // stronger farther away: not a real fit
  CHECK(c.clamped && near(c.nExp, RQS_DIST_N_MIN, 1e-6), "held at the lower limit");
  CHECK(c.estimate(-200, d, lo, hi) && d == RQS_DIST_MAX_M, "estimates are held inside 0.3 to 200 m");

  uint8_t buf[64];
  size_t len = s.save(buf, sizeof buf);
  Ranger t;
  CHECK(len == 2 + 4 * 8 + 1 && t.load(buf, len) && t.n == 4 && near(t.nExp, s.nExp, 1e-6), "flash round trip");
  buf[5] ^= 1;
  CHECK(!t.load(buf, len), "a corrupt copy is refused");
  for (int i = 0; i < 8; i++) s.add(20.0f + i, -60);
  CHECK(s.n == RQS_DIST_MAX_POINTS, "at most %d points", RQS_DIST_MAX_POINTS);
}

static void testGroundMode() {
  Station st;
  st.eng->groundMode = true;
  st.settle();
  float worst = 0;
  for (int i = 0; i < 600; i++) { const Result& r = st.tick(); if (r.rqs > worst) worst = r.rqs; }
  const Result& q = st.eng->result();
  CHECK(q.tag == TAG_GROUND && worst < RQS_LABEL_WEAK && !isfinite(q.sPass), "quiet: GROUND, worst %.1f", worst);
  CHECK(!isfinite(q.distM) && q.distCal == 0, "no calibration, no distance");
  for (int i = 0; i < 30; i++) st.tick(-50);
  const Result& s = st.eng->result();
  CHECK(s.detected && near(s.rqs, 100, 1e-3) && s.label == LABEL_STRONG, "strong tower signal: %.1f %s", s.rqs,
        labelText(s.label));
  char js[700];
  appendJson(s, true, js, sizeof js);
  CHECK(strstr(js, "\"rqsTag\":\"GROUND\"") && strstr(js, "\"rqsPass\":null") && strstr(js, "\"distM\":null") &&
            strstr(js, "\"distCal\":0"),
        "ground JSON: %s", js);
  CHECK(!isfinite(s.passElDeg) && s.nextPassUtc == 0, "no satellite fields in ground mode");
}

static void testDistanceEndToEnd() {
  // SYNTHETIC tower: -20 dBm at 1 m, falling 22 dB per decade (n = 2.2), over the station's -75 dBm floor.
  auto tower = [](double m) { return -20 - 22 * log10(m); };
  Station st;
  st.eng->groundMode = true;
  st.settle();
  for (int i = 0; i < 5; i++) st.tick();
  CHECK(st.eng->addDistancePoint(1) == DIST_NO_SIGNAL, "no point without a signal");
  CHECK(st.eng->addDistancePoint(500) == DIST_BAD_DISTANCE, "500 m is refused");
  for (double m : {1.0, 3.0, 10.0}) {
    for (int i = 0; i < 40; i++) st.tick(tower(m));   // 80 s: the Kalman filter settles
    CHECK(st.eng->addDistancePoint((float)m) == DIST_ADDED, "point at %.0f m", m);
  }
  CHECK(near(st.eng->ranger.nExp, 2.2, 0.15), "fitted exponent %.2f (true 2.2)", st.eng->ranger.nExp);
  for (double m : {2.0, 5.0, 15.0}) {
    for (int i = 0; i < 40; i++) st.tick(tower(m));
    const Result& r = st.eng->result();
    CHECK(isfinite(r.distM) && fabs(r.distM - m) / m < 0.15 && r.distLoM < m && r.distHiM > m,
          "at %.0f m: estimate %.2f (%.2f to %.2f)", m, r.distM, r.distLoM, r.distHiM);
    NOTE("tower at %.0f m: estimate %.2f m (%.2f to %.2f), signal %.1f dBm", m, r.distM, r.distLoM, r.distHiM, r.signalDbm);
  }
  for (int i = 0; i < 40; i++) st.tick();
  CHECK(!isfinite(st.eng->result().distM), "tower off: no distance");
  CHECK(st.eng->result().distCal == 3, "three points kept");
}

// The file tools/make-passes.mjs writes (run.mjs passes its path and pass count), read by the C++ parser.
static const char* g_schedFile = nullptr;
static int g_schedCount = -1;

static void testGeneratedSchedule() {
  if (!g_schedFile) { NOTE("no --schedule file given; skipped"); return; }
  FILE* f = fopen(g_schedFile, "rb");
  CHECK(f != nullptr, "open %s", g_schedFile);
  if (!f) return;
  std::string js;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) js.append(buf, n);
  fclose(f);
  std::unique_ptr<Schedule> s(new Schedule);
  CHECK(parseSchedule(js.c_str(), js.size(), *s) && s->ok, "the generator's file parses (%zu bytes)", js.size());
  CHECK((int)s->n == g_schedCount, "all %d passes read, got %u", g_schedCount, (unsigned)s->n);
  CHECK(s->validUntil - s->generated == 48 * 3600 && s->stepS == 30, "48 h at 30 s steps");
  for (uint8_t i = 0; i < s->n; i++) {
    const Pass& p = s->p[i];
    float mid = s->elevationAt(p, (p.aos + p.los) / 2);
    CHECK(p.nEl >= 2 && mid >= 4.9f && mid <= p.maxEl + 0.1f, "pass %u: %u points, %.1f deg halfway, max %.1f",
          (unsigned)i, (unsigned)p.nEl, mid, p.maxEl);
  }
  NOTE("generated schedule: %u passes, %zu bytes", (unsigned)s->n, js.size());
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) g_verbose = true;
    if (!strcmp(argv[i], "--schedule") && i + 2 < argc) { g_schedFile = argv[i + 1]; g_schedCount = atoi(argv[i + 2]); i += 2; }
  }
  struct { const char* name; void (*fn)(); } tests[] = {
    {"percentile and MAD with the guard window", testStatsAndGuard},
    {"warm-up", testWarmUp},
    {"N in a row and hysteresis", testDetectionAndHysteresis},
    {"freeze and unfreeze", testFreeze},
    {"freeze limit after a lasting step", testFreezeLimit},
    {"Friis range and expected power", testFriis},
    {"Pearson correlation", testPearson},
    {"score, labels, pass part and health", testScoreLabelsAndHealth},
    {"BMP388 temperature handling", testTemperature},
    {"schedule parser", testScheduleParser},
    {"tags", testTags},
    {"a full pass, the same rise outside one, a flat rise", testFullPass},
    {"no false alarms on noise", testNoFalseAlarms},
    {"JSON fields and size", testJsonAndSize},
    {"UTC format and the q report", testFormatAndReport},
    {"the schedule tools/make-passes.mjs writes", testGeneratedSchedule},
    {"distance fit (log-distance path loss)", testRanger},
    {"ground mode", testGroundMode},
    {"distance end to end with a synthetic tower", testDistanceEndToEnd},
  };
  for (auto& t : tests) {
    g_test = t.name;
    int before = g_failures;
    t.fn();
    printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
  }
  printf("\n%d checks, %d failed. Every input is SYNTHETIC (made up in tests/firmware/rqs_test.cpp).\n", g_checks,
         g_failures);
  return g_failures ? 1 : 0;
}
