// Reception quality score (SPEC.md section 14): one 0 to 100 score from the RF reading, once per 2 s sample.
// Plain C++17 with no Arduino headers, so tests/firmware/rqs_test.cpp compiles this same file on a PC.
// Every constant is at the top. PLACEHOLDER marks a value that is a guess until it is measured; "fixed" marks
// physics or statistics.
//
//   1. Temperature: the BMP388 only (never the thermistors), sanity checked, smoothed over 60 s, optional compensation.
//   2. Causal OS-CFAR baseline (Rohling 1983): 15 min ring, 30 s guard, 20th percentile, 1.4826 x MAD spread.
//   3. Detection: K sigma above the baseline, N samples in a row, with hysteresis to clear.
//   4. Parts: strength and detection.
//   5. Pass match: Pearson r between the excess and the free-space power curve (Friis 1946) during a scheduled pass.
//   6. Health: battery charge and box temperature.
//   7. Score, label and tag.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

// ---------- Baseline and detection (SPEC 14.5, 14.6) ----------
#define RQS_RING_N            450       // PLACEHOLDER: 15 min of 2 s samples
#define RQS_GUARD_N           15        // PLACEHOLDER: the newest 30 s are left out of the baseline
#define RQS_MIN_TRAIN_N       150       // PLACEHOLDER: no score until this many training cells (5 min)
#define RQS_BASE_PCT          20.0f     // PLACEHOLDER: baseline = this percentile of the training cells
#define RQS_MAD_SCALE         1.4826f   // fixed: scales the MAD to a standard deviation for normal noise
#define RQS_SIGMA_MIN_DB      0.3f      // PLACEHOLDER: the Kalman filter makes the raw spread very small
#define RQS_K                 4.0f      // PLACEHOLDER: threshold = baseline + K x sigma
#define RQS_N_ON              3         // PLACEHOLDER: samples in a row above the threshold to detect
#define RQS_N_OFF             3         // PLACEHOLDER: samples in a row below baseline + (K - 1) x sigma to clear
#define RQS_UNFREEZE_S        60.0f     // PLACEHOLDER: the ring takes samples again this long after a detection clears
#define RQS_MAX_FREEZE_S      900.0f    // PLACEHOLDER: longest freeze; a pass above 5 degrees lasts about 13 min at most

// ---------- Parts and score (14.7, 14.10) ----------
#define RQS_D_FULL_DB         12.0f     // PLACEHOLDER: strength part is 1 at this many dB above the baseline
#define RQS_W_SNR             0.50f     // PLACEHOLDER: weights of the three parts
#define RQS_W_DET             0.20f     // PLACEHOLDER
#define RQS_W_PASS            0.30f     // PLACEHOLDER
#define RQS_LABEL_WEAK        15.0f     // PLACEHOLDER: under this, NO SIGNAL
#define RQS_LABEL_GOOD        40.0f     // PLACEHOLDER
#define RQS_LABEL_STRONG      70.0f     // PLACEHOLDER

// ---------- Pass match (14.8) ----------
#define RQS_PASS_BASE         0.4f      // PLACEHOLDER: pass part inside a window, before the shape is known
#define RQS_CORR_LO           0.3f      // PLACEHOLDER: shape part is 0 at this correlation...
#define RQS_CORR_SPAN         0.6f      // PLACEHOLDER: ...and 1 at RQS_CORR_LO + this
#define RQS_CORR_MIN_N        10        // PLACEHOLDER: pairs needed before the correlation counts (20 s)
#define RQS_PASS_UNKNOWN      0.5f      // PLACEHOLDER: pass part with no valid schedule, and in TEST MODE
#define RQS_RE_KM             6371.0    // fixed: mean Earth radius
#define RQS_ORBIT_H_KM        820.0     // fixed: Meteor-M2-4 orbit height
#define RQS_SCHED_EVERY_S     21600     // PLACEHOLDER: fetch the schedule every 6 h...
#define RQS_SCHED_RETRY_S     900       // PLACEHOLDER: ...or 15 min after a failed fetch
#define RQS_MAX_PASSES        24        // fixed: memory size (48 h holds about 8 to 12 passes)
#define RQS_MAX_EL            40        // fixed: memory size (20 min at 30 s per pass)
#define RQS_CLOCK_SET_UTC     1767225600LL   // fixed: 2026-01-01 00:00 UTC; an earlier clock is not set yet

// ---------- Temperature, BMP388 only (14.4) ----------
#define RQS_ALPHA_T_DB_PER_C  0.0f      // PLACEHOLDER: 0 until the warm-up test (docs/rqs-validation.md, test b)
#define RQS_T_REF_C           25.0f     // fixed: the AD8307 datasheet's specification temperature
#define RQS_TEMP_TAU_S        60.0f     // PLACEHOLDER: smoothing time constant; also the gap that restarts the filter
#define RQS_TEMP_MIN_C        -20.0f    // PLACEHOLDER: sanity range of a raw sample
#define RQS_TEMP_MAX_C        85.0f     // PLACEHOLDER
#define RQS_TEMP_JUMP_C       5.0f      // PLACEHOLDER: a bigger change from the previous sample is rejected

// ---------- Health (14.9) ----------
#define RQS_BATT_FULL_PCT     20.0f     // PLACEHOLDER: battery factor is 1 at this charge or more...
#define RQS_BATT_H_EMPTY      0.5f      // PLACEHOLDER: ...falling to this at 0%
#define RQS_HOT_START_C       45.0f     // PLACEHOLDER: temperature factor is 1 up to here...
#define RQS_HOT_END_C         60.0f     // PLACEHOLDER: ...falling to RQS_H_HOT here, and staying there above
#define RQS_H_HOT             0.7f      // PLACEHOLDER

namespace rqs {

enum Label : uint8_t { LABEL_WARMING_UP, LABEL_NO_SIGNAL, LABEL_WEAK, LABEL_GOOD, LABEL_STRONG };
enum Tag : uint8_t { TAG_PASS, TAG_NO_PASS, TAG_NO_SCHEDULE, TAG_TEST };
// Why the schedule is, or is not, in use right now.
enum SchedState : uint8_t { SCHED_OK, SCHED_NO_CLOCK, SCHED_NONE, SCHED_EXPIRED };

inline const char* labelText(Label l) {
  switch (l) {
    case LABEL_NO_SIGNAL: return "NO SIGNAL";
    case LABEL_WEAK: return "WEAK";
    case LABEL_GOOD: return "GOOD";
    case LABEL_STRONG: return "STRONG";
    default: return "WARMING UP";
  }
}

inline const char* tagText(Tag t) {
  switch (t) {
    case TAG_PASS: return "PASS";
    case TAG_NO_PASS: return "NO PASS";
    case TAG_TEST: return "TEST";
    default: return "NO SCHEDULE";
  }
}

// At most 8 characters, for the 16 character LCD.
inline const char* tagShort(Tag t) { return t == TAG_NO_SCHEDULE ? "NO SCHED" : tagText(t); }

inline const char* schedText(SchedState s) {
  switch (s) {
    case SCHED_OK: return "ok";
    case SCHED_NO_CLOCK: return "clock not set (NTP)";
    case SCHED_EXPIRED: return "expired";
    default: return "none loaded";
  }
}

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// ---------- Geometry (14.8) ----------

// Distance from the station to the satellite at elevation elDeg, on a spherical Earth.
inline double slantRangeKm(double elDeg) {
  const double rad = 3.14159265358979323846 / 180.0;
  const double rs = RQS_RE_KM + RQS_ORBIT_H_KM;
  const double c = RQS_RE_KM * cos(elDeg * rad);
  return sqrt(rs * rs - c * c) - RQS_RE_KM * sin(elDeg * rad);
}

// Friis: received power falls as 1 / r^2, so relative to the satellite straight overhead (r = h), in dB.
inline double expectedRelDb(double elDeg) { return -20.0 * log10(slantRangeKm(elDeg) / RQS_ORBIT_H_KM); }

// ---------- Statistics (14.5) ----------

// The order statistic for percentile pct (0 to 100): the value at rank round(pct / 100 x (n - 1)). Reorders v.
inline float orderStat(float* v, size_t n, float pct) {
  size_t k = (size_t)lroundf(pct / 100.0f * (float)(n - 1));
  if (k >= n) k = n - 1;
  std::nth_element(v, v + k, v + n);
  return v[k];
}

// Reorders v.
inline float median(float* v, size_t n) {
  size_t m = n / 2;
  std::nth_element(v, v + m, v + n);
  float upper = v[m];
  if (n % 2) return upper;
  return 0.5f * (*std::max_element(v, v + m) + upper);
}

// 1.4826 x the median absolute deviation from the median. Overwrites v.
inline float madSigma(float* v, size_t n) {
  float med = median(v, n);
  for (size_t i = 0; i < n; i++) v[i] = fabsf(v[i] - med);
  return RQS_MAD_SCALE * median(v, n);
}

struct Stats {
  bool ok = false;           // enough training cells
  float baseline = NAN, sigma = NAN;
  uint16_t training = 0;
};

// The last RQS_RING_N compensated readings, oldest first.
class Ring {
 public:
  void clear() { count_ = head_ = 0; }
  void push(float x) {
    buf_[head_] = x;
    head_ = (uint16_t)((head_ + 1) % RQS_RING_N);
    if (count_ < RQS_RING_N) count_++;
  }
  uint16_t size() const { return count_; }
  uint16_t training() const { return count_ > RQS_GUARD_N ? (uint16_t)(count_ - RQS_GUARD_N) : 0; }
  float at(uint16_t i) const { return buf_[(head_ + RQS_RING_N - count_ + i) % RQS_RING_N]; }

  // Baseline and sigma of the training cells: everything except the newest RQS_GUARD_N samples.
  // scratch must hold RQS_RING_N floats.
  Stats stats(float* scratch) const {
    Stats s;
    s.training = training();
    if (s.training < RQS_MIN_TRAIN_N) return s;
    for (uint16_t i = 0; i < s.training; i++) scratch[i] = at(i);
    s.baseline = orderStat(scratch, s.training, RQS_BASE_PCT);
    s.sigma = madSigma(scratch, s.training);
    if (!(s.sigma >= RQS_SIGMA_MIN_DB)) s.sigma = RQS_SIGMA_MIN_DB;
    s.ok = true;
    return s;
  }

 private:
  float buf_[RQS_RING_N];
  uint16_t count_ = 0, head_ = 0;
};

// Pearson correlation from running sums, so adding a pair costs nothing.
struct Pearson {
  uint32_t n = 0;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  void clear() { n = 0; sx = sy = sxx = syy = sxy = 0; }
  void add(double x, double y) {
    n++;
    sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
  }
  // NaN with fewer than minN pairs, or when either side has not varied.
  double r(uint32_t minN = RQS_CORR_MIN_N) const {
    if (n < minN || n < 2) return NAN;
    double vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
    if (vx <= 1e-9 * n * sxx + 1e-12 || vy <= 1e-9 * n * syy + 1e-12) return NAN;
    double v = (n * sxy - sx * sy) / sqrt(vx * vy);
    return v > 1 ? 1 : (v < -1 ? -1 : v);
  }
};

// ---------- Temperature (14.4) ----------

struct TempFilter {
  bool havePrev = false, haveSmooth = false;
  float prevRaw = 0, smooth = 0, sinceGoodS = 0;

  // True when this raw BMP388 sample is good; `smooth` then holds the smoothed value.
  bool step(float raw, float dtS) {
    bool inRange = isfinite(raw) && raw >= RQS_TEMP_MIN_C && raw <= RQS_TEMP_MAX_C;
    bool good = inRange && (!havePrev || fabsf(raw - prevRaw) <= RQS_TEMP_JUMP_C);
    if (inRange) { prevRaw = raw; havePrev = true; }
    if (!good) { sinceGoodS += dtS; return false; }
    if (!haveSmooth || sinceGoodS > RQS_TEMP_TAU_S) smooth = raw;          // first sample, or after a long gap
    else smooth += (1.0f - expf(-dtS / RQS_TEMP_TAU_S)) * (raw - smooth);
    haveSmooth = true;
    sinceGoodS = 0;
    return true;
  }
};

// ---------- Health and score (14.9, 14.10) ----------

inline float hBatt(float socPct) {
  if (!isfinite(socPct)) return 1.0f;          // unknown charge: no penalty, the site says BATT N/A
  if (socPct >= RQS_BATT_FULL_PCT) return 1.0f;
  float s = socPct < 0 ? 0 : socPct;
  return RQS_BATT_H_EMPTY + (1.0f - RQS_BATT_H_EMPTY) * s / RQS_BATT_FULL_PCT;
}

inline float hTemp(float tempC) {
  if (!isfinite(tempC) || tempC <= RQS_HOT_START_C) return 1.0f;
  if (tempC >= RQS_HOT_END_C) return RQS_H_HOT;
  return 1.0f - (1.0f - RQS_H_HOT) * (tempC - RQS_HOT_START_C) / (RQS_HOT_END_C - RQS_HOT_START_C);
}

inline float passPart(Tag tag, double corr) {
  if (tag == TAG_TEST || tag == TAG_NO_SCHEDULE) return RQS_PASS_UNKNOWN;
  if (tag == TAG_NO_PASS) return 0.0f;
  float shape = isfinite(corr) ? clamp01((float)((corr - RQS_CORR_LO) / RQS_CORR_SPAN)) : 0.0f;
  return RQS_PASS_BASE + (1.0f - RQS_PASS_BASE) * shape;
}

inline float score(float sSnr, float sDet, float sPass, float health) {
  float v = 100.0f * health * (RQS_W_SNR * sSnr + RQS_W_DET * sDet + RQS_W_PASS * sPass);
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}

inline Label labelFor(float rqs) {
  if (!isfinite(rqs)) return LABEL_WARMING_UP;
  if (rqs < RQS_LABEL_WEAK) return LABEL_NO_SIGNAL;
  if (rqs < RQS_LABEL_GOOD) return LABEL_WEAK;
  if (rqs < RQS_LABEL_STRONG) return LABEL_GOOD;
  return LABEL_STRONG;
}

// ---------- Pass schedule (14.8) ----------

struct Pass {
  int64_t aos = 0, los = 0;   // Unix seconds, UTC
  float maxEl = 0;
  uint8_t nEl = 0;
  int16_t elTenths[RQS_MAX_EL];   // elevation every stepS from aos, the last one at los, in 0.1 degree
};

struct Schedule {
  bool ok = false;
  int64_t generated = 0, validUntil = 0;
  int32_t stepS = 30;
  uint8_t n = 0;
  Pass p[RQS_MAX_PASSES];

  void clear() { ok = false; generated = validUntil = 0; stepS = 30; n = 0; }

  // Linear between the points; point i sits at min(aos + i x stepS, los).
  float elevationAt(const Pass& ps, int64_t t) const {
    if (ps.nEl == 0) return NAN;
    double idx = (double)(t - ps.aos) / stepS;
    if (idx <= 0) return ps.elTenths[0] / 10.0f;
    int i = (int)idx;
    if (i >= ps.nEl - 1) return ps.elTenths[ps.nEl - 1] / 10.0f;
    int64_t t0 = ps.aos + (int64_t)i * stepS;
    int64_t t1 = std::min(ps.aos + (int64_t)(i + 1) * stepS, ps.los);
    double f = t1 > t0 ? (double)(t - t0) / (double)(t1 - t0) : 1.0;
    if (f > 1) f = 1;
    return (float)((ps.elTenths[i] + f * (ps.elTenths[i + 1] - ps.elTenths[i])) / 10.0);
  }
};

namespace json {

// A tiny reader for data/passes.json: enough JSON to read numbers, arrays and objects, and skip anything else.
struct Cur {
  const char* p;
  const char* end;
  int depth;
};

inline void ws(Cur& c) {
  while (c.p < c.end && (*c.p == ' ' || *c.p == '\n' || *c.p == '\r' || *c.p == '\t')) c.p++;
}

inline bool lit(Cur& c, char ch) {
  ws(c);
  if (c.p < c.end && *c.p == ch) { c.p++; return true; }
  return false;
}

// Reads a string into out (cut to cap - 1 characters); out may be null to skip it.
inline bool str(Cur& c, char* out, size_t cap) {
  ws(c);
  if (c.p >= c.end || *c.p != '"') return false;
  c.p++;
  size_t n = 0;
  while (c.p < c.end && *c.p != '"') {
    if (*c.p == '\\' && ++c.p >= c.end) return false;
    if (out && n + 1 < cap) out[n++] = *c.p;
    c.p++;
  }
  if (c.p >= c.end) return false;
  c.p++;
  if (out && cap) out[n] = 0;
  return true;
}

inline bool num(Cur& c, double& v) {
  ws(c);
  char b[32];
  size_t n = 0;
  while (c.p < c.end && n < sizeof b - 1 && *c.p && strchr("+-0123456789.eE", *c.p)) b[n++] = *c.p++;
  if (!n) return false;
  b[n] = 0;
  char* stop = nullptr;
  v = strtod(b, &stop);
  return stop == b + n && isfinite(v);
}

inline bool word(Cur& c, const char* w) {
  size_t n = strlen(w);
  if ((size_t)(c.end - c.p) < n || strncmp(c.p, w, n) != 0) return false;
  c.p += n;
  return true;
}

inline bool skip(Cur& c) {
  ws(c);
  if (c.p >= c.end || ++c.depth > 8) return false;
  bool ok;
  char ch = *c.p;
  if (ch == '"') {
    ok = str(c, nullptr, 0);
  } else if (ch == '{' || ch == '[') {
    char close = ch == '{' ? '}' : ']';
    c.p++;
    ok = true;
    if (!lit(c, close)) {
      do {
        if (ch == '{') ok = str(c, nullptr, 0) && lit(c, ':');
        ok = ok && skip(c);
      } while (ok && lit(c, ','));
      ok = ok && lit(c, close);
    }
  } else if (ch == 't' || ch == 'f' || ch == 'n') {
    ok = word(c, "true") || word(c, "false") || word(c, "null");
  } else {
    double v;
    ok = num(c, v);
  }
  c.depth--;
  return ok;
}

inline bool pass(Cur& c, Pass& ps) {
  if (!lit(c, '{')) return false;
  bool haveAos = false, haveLos = false;
  ps.nEl = 0;
  if (!lit(c, '}')) {
    do {
      char key[16];
      double v;
      if (!str(c, key, sizeof key) || !lit(c, ':')) return false;
      if (!strcmp(key, "aos") || !strcmp(key, "los") || !strcmp(key, "maxEl")) {
        if (!num(c, v)) return false;
        if (key[0] == 'a') { ps.aos = (int64_t)v; haveAos = true; }
        else if (key[0] == 'l') { ps.los = (int64_t)v; haveLos = true; }
        else ps.maxEl = (float)v;
      } else if (!strcmp(key, "el")) {
        if (!lit(c, '[')) return false;
        if (!lit(c, ']')) {
          do {
            if (!num(c, v) || v < -5 || v > 91 || ps.nEl >= RQS_MAX_EL) return false;
            ps.elTenths[ps.nEl++] = (int16_t)lround(v * 10);
          } while (lit(c, ','));
          if (!lit(c, ']')) return false;
        }
      } else if (!skip(c)) {
        return false;
      }
    } while (lit(c, ','));
    if (!lit(c, '}')) return false;
  }
  return haveAos && haveLos && ps.los > ps.aos && ps.nEl >= 1;
}

}  // namespace json

// Reads data/passes.json. False on anything malformed; out.ok says whether it can be used.
// Passes past RQS_MAX_PASSES are read and dropped (the file is in time order, so the nearest are kept).
inline bool parseSchedule(const char* s, size_t len, Schedule& out) {
  out.clear();
  json::Cur c{s, s + len, 0};
  bool haveGen = false, haveValid = false;
  Pass spare;
  if (!json::lit(c, '{')) return false;
  if (!json::lit(c, '}')) {
    do {
      char key[16];
      double v;
      if (!json::str(c, key, sizeof key) || !json::lit(c, ':')) return false;
      if (!strcmp(key, "generated") || !strcmp(key, "validUntil") || !strcmp(key, "step")) {
        if (!json::num(c, v)) return false;
        if (key[0] == 'g') { out.generated = (int64_t)v; haveGen = true; }
        else if (key[0] == 'v') { out.validUntil = (int64_t)v; haveValid = true; }
        else out.stepS = (int32_t)v;
      } else if (!strcmp(key, "passes")) {
        if (!json::lit(c, '[')) return false;
        if (!json::lit(c, ']')) {
          do {
            Pass& ps = out.n < RQS_MAX_PASSES ? out.p[out.n] : spare;
            if (!json::pass(c, ps)) return false;
            if (out.n > 0 && &ps != &spare && ps.aos < out.p[out.n - 1].los) return false;   // out of order or overlapping
            if (&ps != &spare) out.n++;
          } while (json::lit(c, ','));
          if (!json::lit(c, ']')) return false;
        }
      } else if (!json::skip(c)) {
        return false;
      }
    } while (json::lit(c, ','));
    if (!json::lit(c, '}')) return false;
  }
  json::ws(c);
  if (c.p != c.end) return false;   // trailing junk
  out.ok = haveGen && haveValid && out.validUntil > out.generated && out.stepS >= 1 && out.stepS <= 600;
  return out.ok;
}

// "2026-11-01 14:05:12" for a Unix time in seconds (UTC), without depending on the platform's time functions.
inline void formatUtc(int64_t t, char* buf, size_t cap) {
  int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
  int64_t sec = t - days * 86400;
  // Civil date from days since 1970-01-01 (H. Hinnant's algorithm).
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp < 10 ? mp + 3 : mp - 9;
  int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
  snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d:%02d", (int)y, (int)m, (int)d, (int)(sec / 3600), (int)(sec / 60 % 60),
           (int)(sec % 60));
}

// ---------- The engine ----------

struct Input {
  float dtS = 2;          // seconds since the previous step
  float rfDbm = NAN;      // rfFiltDbm (Kalman filtered AD8307 reading)
  float tempC = NAN;      // raw BMP388 bmp.temperature; NaN when bmpOk is false or the read failed
  float socPct = NAN;     // battery estimate; NaN when unknown
  int64_t nowUtc = 0;     // Unix seconds from NTP; 0 when the clock is not set
};

struct Result {
  bool scored = false;    // false while warming up
  float rqs = NAN;
  Label label = LABEL_WARMING_UP;
  Tag tag = TAG_NO_SCHEDULE;
  float excessDb = NAN, baselineDbm = NAN, thresholdDbm = NAN, sigmaDb = NAN;
  bool detected = false;
  float passElDeg = NAN;  // inside a pass window only
  float passCorr = NAN;
  int64_t nextPassUtc = 0;   // 0 when unknown
  float tempC = NAN;      // the smoothed BMP388 value used; NaN when the sample was rejected
  bool tempOk = false, battKnown = false;
  float compDb = 0, rfComp = NAN;
  float sSnr = NAN, sDet = NAN, sPass = NAN, health = NAN;   // the four parts; NaN while warming up
  float hBatt = 1, hTemp = 1;
  uint16_t training = 0;
  bool frozen = false;
  float frozenS = 0;
  SchedState sched = SCHED_NONE;
};

typedef void (*LineFn)(const char* line, void* ctx);

class Engine {
 public:
  bool testMode = false;                    // serial `t`; never saved
  float alphaT = RQS_ALPHA_T_DB_PER_C;      // dB per C; a member so the PC tests can try a nonzero value

  void setSchedule(const Schedule& s) { sched_ = s; }
  const Schedule& schedule() const { return sched_; }
  const Result& result() const { return res_; }
  uint32_t pushes() const { return pushes_; }

  SchedState schedState(int64_t nowUtc) const {
    if (nowUtc < RQS_CLOCK_SET_UTC) return SCHED_NO_CLOCK;
    if (!sched_.ok) return SCHED_NONE;
    if (nowUtc >= sched_.validUntil) return SCHED_EXPIRED;
    return SCHED_OK;
  }

  const Result& step(const Input& in) {
    Result r;
    float dt = in.dtS > 0 ? in.dtS : 2.0f;

    // 1. Temperature: the BMP388 only.
    r.tempOk = temp_.step(in.tempC, dt);
    r.tempC = r.tempOk ? temp_.smooth : NAN;
    r.compDb = (r.tempOk && alphaT != 0.0f) ? alphaT * (temp_.smooth - RQS_T_REF_C) : 0.0f;
    bool rfOk = isfinite(in.rfDbm);
    r.rfComp = rfOk ? in.rfDbm - r.compDb : NAN;

    // 2 and 3. Baseline from the ring as it stands (this sample is the cell under test), then detection.
    if (statsDirty_) { stats_ = ring_.stats(scratch_); statsDirty_ = false; }
    r.training = stats_.training;
    r.scored = rfOk && stats_.ok;
    if (r.scored) {
      r.baselineDbm = stats_.baseline;
      r.sigmaDb = stats_.sigma;
      r.thresholdDbm = stats_.baseline + RQS_K * stats_.sigma;
      r.excessDb = r.rfComp - stats_.baseline;
      if (!detected_) {
        if (r.rfComp > r.thresholdDbm) {
          if (++onCount_ >= RQS_N_ON) { detected_ = true; onCount_ = offCount_ = 0; }
        } else {
          onCount_ = 0;
        }
      } else {
        if (r.rfComp < stats_.baseline + (RQS_K - 1.0f) * stats_.sigma) {
          if (++offCount_ >= RQS_N_OFF) { detected_ = false; onCount_ = offCount_ = 0; }
        } else {
          offCount_ = 0;
        }
      }
    } else {
      detected_ = false;
      onCount_ = offCount_ = 0;
    }
    r.detected = detected_;

    // Freeze: a detection cannot raise its own baseline.
    if (detected_) {
      if (!freezeSpent_) { frozen_ = true; releaseS_ = RQS_UNFREEZE_S; }
    } else {
      freezeSpent_ = false;
      if (frozen_) { releaseS_ -= dt; if (releaseS_ <= 0) frozen_ = false; }
    }
    if (frozen_) {
      frozenS_ += dt;
      if (frozenS_ >= RQS_MAX_FREEZE_S) { frozen_ = false; freezeSpent_ = detected_; }
    }
    if (!frozen_) frozenS_ = 0;
    if (rfOk && !frozen_) { ring_.push(r.rfComp); statsDirty_ = true; pushes_++; }
    r.frozen = frozen_;
    r.frozenS = frozenS_;

    // 5. Pass match.
    r.sched = schedState(in.nowUtc);
    const Pass* cur = nullptr;
    if (r.sched == SCHED_OK) {
      for (uint8_t i = 0; i < sched_.n; i++) {
        const Pass& ps = sched_.p[i];
        if (in.nowUtc >= ps.aos && in.nowUtc <= ps.los) cur = &ps;
        if (ps.aos > in.nowUtc) { r.nextPassUtc = ps.aos; break; }
      }
    }
    if (cur) {
      if (cur->aos != passAos_) { passAos_ = cur->aos; corr_.clear(); }
      r.passElDeg = sched_.elevationAt(*cur, in.nowUtc);
      if (r.scored) corr_.add(r.excessDb, expectedRelDb(r.passElDeg));
      r.passCorr = (float)corr_.r();
    } else {
      passAos_ = 0;
    }
    if (testMode) r.tag = TAG_TEST;
    else if (r.sched != SCHED_OK) r.tag = TAG_NO_SCHEDULE;
    else r.tag = cur ? TAG_PASS : TAG_NO_PASS;

    // 6. Health.
    r.battKnown = isfinite(in.socPct);
    r.hBatt = hBatt(in.socPct);
    r.hTemp = r.tempOk ? hTemp(r.tempC) : 1.0f;

    // 7. Score. The pass part counts only while something is detected: otherwise NO SCHEDULE (0.5) alone would give
    // plain noise 15 points (WEAK), and noise that happens to correlate with the curve would score during every pass.
    if (r.scored) {
      r.sSnr = clamp01(r.excessDb / RQS_D_FULL_DB);
      r.sDet = detected_ ? 1.0f : 0.0f;
      r.sPass = passPart(r.tag, r.passCorr);
      r.health = r.hBatt * r.hTemp;
      r.rqs = score(r.sSnr, r.sDet, r.sPass * r.sDet, r.health);
    }
    r.label = labelFor(r.rqs);
    res_ = r;
    return res_;
  }

  // The `q` report, one line at a time.
  void debugLines(LineFn fn, void* ctx, int64_t nowUtc) const {
    char b[120], t[24];
    const Result& r = res_;
    if (r.scored) {
      snprintf(b, sizeof b, "RQS %.1f %s, tag %s%s", (double)r.rqs, labelText(r.label), tagText(r.tag),
               testMode ? " (TEST MODE on, `t` turns it off)" : "");
    } else {
      snprintf(b, sizeof b, "RQS warming up: %u of %u training samples, tag %s", (unsigned)r.training,
               (unsigned)RQS_MIN_TRAIN_N, tagText(r.tag));
    }
    fn(b, ctx);
    if (r.scored) {
      snprintf(b, sizeof b, "Baseline %.2f dBm, sigma %.2f dB, threshold %.2f dBm, excess %+.2f dB",
               (double)r.baselineDbm, (double)r.sigmaDb, (double)r.thresholdDbm, (double)r.excessDb);
      fn(b, ctx);
      snprintf(b, sizeof b, "Parts: strength %.2f, detection %.0f, pass %.2f, health %.2f (battery %.2f%s, temperature %.2f)",
               (double)r.sSnr, (double)r.sDet, (double)r.sPass, (double)r.health, (double)r.hBatt,
               r.battKnown ? "" : " N/A", (double)r.hTemp);
      fn(b, ctx);
    }
    snprintf(b, sizeof b, "Detection %s; ring %u samples; %s", r.detected ? "ON" : "off", (unsigned)ring_.size(),
             r.frozen ? "frozen" : "taking samples");
    fn(b, ctx);
    if (r.frozen) {
      snprintf(b, sizeof b, "  frozen %.0f s of at most %.0f s", (double)r.frozenS, (double)RQS_MAX_FREEZE_S);
      fn(b, ctx);
    }
    if (r.tempOk) {
      snprintf(b, sizeof b, "Temperature (BMP388) %.2f C smoothed; compensation %+.2f dB (ALPHA_T %.4f dB/C)",
               (double)r.tempC, (double)r.compDb, (double)alphaT);
    } else {
      snprintf(b, sizeof b, "Temperature: TEMP N/A (BMP388 missing, failed, out of range or jumped); no compensation");
    }
    fn(b, ctx);
    if (nowUtc >= RQS_CLOCK_SET_UTC) {
      formatUtc(nowUtc, t, sizeof t);
      snprintf(b, sizeof b, "Clock %s UTC", t);
    } else {
      snprintf(b, sizeof b, "Clock not set yet (NTP)");
    }
    fn(b, ctx);
    snprintf(b, sizeof b, "Schedule: %s, %u passes", schedText(r.sched), (unsigned)sched_.n);
    fn(b, ctx);
    if (sched_.ok) {
      formatUtc(sched_.validUntil, t, sizeof t);
      snprintf(b, sizeof b, "  valid until %s UTC", t);
      fn(b, ctx);
    }
    if (isfinite(r.passElDeg)) {
      snprintf(b, sizeof b, "  IN A PASS: expected elevation %.1f deg, correlation %s", (double)r.passElDeg,
               isfinite(r.passCorr) ? "" : "not known yet");
      if (isfinite(r.passCorr)) snprintf(b + strlen(b), sizeof b - strlen(b), "%.2f", (double)r.passCorr);
      fn(b, ctx);
    }
    int shown = 0;
    for (uint8_t i = 0; i < sched_.n && shown < 3; i++) {
      const Pass& ps = sched_.p[i];
      if (ps.los < nowUtc) continue;
      formatUtc(ps.aos, t, sizeof t);
      snprintf(b, sizeof b, "  pass %s UTC, %.1f min, max %.1f deg", t, (double)(ps.los - ps.aos) / 60.0,
               (double)ps.maxEl);
      fn(b, ctx);
      shown++;
    }
  }

 private:
  Ring ring_;
  float scratch_[RQS_RING_N];
  Stats stats_;
  bool statsDirty_ = true;
  TempFilter temp_;
  bool detected_ = false;
  uint8_t onCount_ = 0, offCount_ = 0;
  bool frozen_ = false, freezeSpent_ = false;
  float frozenS_ = 0, releaseS_ = 0;
  uint32_t pushes_ = 0;
  Schedule sched_;
  int64_t passAos_ = 0;
  Pearson corr_;
  Result res_;
};

// ---------- Firebase fields (14.12) ----------

// Appends the RQS fields to a JSON object being built, each as ,"key":value. withParts adds the four parts, which go
// in /station/latest only. Returns the length written, or -1 if it did not fit.
inline int appendJson(const Result& r, bool withParts, char* buf, size_t cap) {
  size_t n = 0;
  bool fits = true;
  auto raw = [&](const char* s) {
    size_t k = strlen(s);
    if (n + k >= cap) { fits = false; return; }
    memcpy(buf + n, s, k);
    n += k;
    buf[n] = 0;
  };
  auto key = [&](const char* k) { raw(",\""); raw(k); raw("\":"); };
  auto number = [&](const char* k, float v, int decimals) {
    char t[24];
    key(k);
    if (!isfinite(v)) { raw("null"); return; }
    snprintf(t, sizeof t, "%.*f", decimals, (double)v);
    raw(t);
  };
  auto text = [&](const char* k, const char* v) { key(k); raw("\""); raw(v); raw("\""); };
  if (cap) buf[0] = 0;
  number("rqs", r.rqs, 1);
  text("rqsLabel", labelText(r.label));
  text("rqsTag", tagText(r.tag));
  number("rfExcessDb", r.excessDb, 2);
  number("rfBaselineDbm", r.baselineDbm, 2);
  number("rfThresholdDbm", r.thresholdDbm, 2);
  number("rfSigmaDb", r.sigmaDb, 2);
  key("rfDetected");
  raw(r.detected ? "true" : "false");
  number("passElDeg", r.passElDeg, 1);
  number("passCorr", r.passCorr, 2);
  key("nextPassEpoch");
  if (r.nextPassUtc > 0) {
    char t[24];
    snprintf(t, sizeof t, "%lu", (unsigned long)r.nextPassUtc);
    raw(t);
  } else {
    raw("null");
  }
  number("rqsTempC", r.tempC, 2);
  if (withParts) {
    number("rqsSnr", r.sSnr, 2);
    number("rqsDet", r.sDet, 2);
    number("rqsPass", r.sPass, 2);
    number("rqsHealth", r.health, 2);
  }
  return fits ? (int)n : -1;
}

}  // namespace rqs
