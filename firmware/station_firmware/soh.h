/*
 * soh.h: battery state of health from normal use (SPEC.md section 14)
 * Satellite V-Dipole Tracking Station, ELEE 4230
 *
 * Plain C++ with no Arduino headers, so the same code runs on the ESP32 and in the PC tests
 * (tests/firmware/soh_test.cpp, run with `npm run test:firmware`).
 *
 * In short:
 *   1. Every 2 s the firmware passes one averaged sample: battery voltage, load current, power, temperature.
 *   2. Anchors are moments when the voltage says how much energy is left: "full" (the charger held the
 *      pack full, then stopped), "rest" (small steady load, voltage settled) and "cutoff" (9.5 V).
 *   3. Each discharge stretch gives one slice: the energy counted between two anchors, and the change in
 *      energy percent (SOE) the voltage table gives for them, each with its uncertainty.
 *   4. A weighted total least squares fit over the last 32 slices (Plett 2011) gives the usable energy C,
 *      and SOH = C / 26.6 Wh x 100 with a plus-or-minus.
 *
 * Search for "PLACEHOLDER" to find every value that is a guess until it is measured.
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------- Pack ----------
#define SOH_RATED_WH           26.6     // label energy (KBT 12V 2400 mAh, 3S Li-ion)
#define SOH_CELLS              3
#define SOH_R_INT_OHM          0.25     // PLACEHOLDER: pack + wiring resistance, for V_oc = V + I x R (same as BATT_R_INT_OHM)

// ---------- Uncertainties ----------
#define SOH_TABLE_SIGMA_MV     15.0     // PLACEHOLDER: how far this pack's curve may sit from the generic table, per cell
#define SOH_RINT_SIGMA_OHM     0.1      // PLACEHOLDER: how far SOH_R_INT_OHM may be off
#define SOH_E_SIGMA_PCT        2.0      // PLACEHOLDER: energy count error (shunt tolerance, INA219 gain) until calibrated...
#define SOH_E_SIGMA_WH         0.02     // PLACEHOLDER: ...plus this much
#define SOH_FLOOR_PCT          3.0      // PLACEHOLDER: added in quadrature to the final plus-or-minus, for the table's shape
#define SOH_ANCHOR_MIN_SIGMA   1.0      // no anchor is trusted to better than 1 SOE point
#define SOH_FULL_SIGMA         1.0      // the charger defines full better than the table does

// ---------- Anchors ----------
#define SOH_FULL_V             12.54    // V_oc at or above this while charging counts as full (4.18 V per cell)...
#define SOH_FULL_HOLD_S        1200     // PLACEHOLDER: ...if held this long, so the charger had time to top the pack off
#define SOH_HOLD_TOL_V         0.020    // the charger still holds the pack while V_oc is within this of its highest value
#define SOH_CUTOFF_V           9.5      // battery voltage at or below this...
#define SOH_CUTOFF_HOLD_S      30       // ...for this long is the cutoff anchor
#define SOH_REST_MAX_MA        300.0    // quasi-rest: current under this...
#define SOH_STEADY_S           120      // ...steady over this window...
#define SOH_STEADY_PCT         5.0      // ...within this percent of the mean...
#define SOH_STEADY_MA          8.0      // ...or this many mA, whichever is larger...
#define SOH_RISE_MAX_MV_MIN    2.0      // ...voltage rising no faster than this...
#define SOH_FALL_MAX_MV_MIN    30.0     // ...and falling slower than this...
#define SOH_SETTLE_S           3600     // ...at least this long after a charge ended or the station started

// ---------- Charger detection (inferred: the INA219 never sees charger current) ----------
#define SOH_SMOOTH_S           30.0     // V_oc smoothing for the detector, so a short load change cannot trip it
#define SOH_CHARGE_RISE_V      0.060    // plugged in: smoothed V_oc this far above the stretch's lowest...
#define SOH_CHARGE_DROP_V      0.060    // unplugged: this far below the highest charging value (or back under SOH_FULL_V)...
#define SOH_CHARGE_HOLD_S      60       // ...for this long

// ---------- Slices ----------
#define SOH_MIN_X              2.0      // SOE points
#define SOH_MIN_E_WH           0.2
#define SOH_TEMP_MIN_C         15.0     // mean temperature over the slice
#define SOH_TEMP_MAX_C         35.0
#define SOH_GAP_MAX_FRAC       0.01     // at most 1% of the slice's time missing
#define SOH_DT_CAP_S           10.0     // a longer step counts only this much; the rest is a gap
#define SOH_LOST_S             60       // no battery reading for this long ends the stretch
#define SOH_V_VALID_MIN        8.0      // readings outside this range mean no battery (USB power while flashing)
#define SOH_V_VALID_MAX        13.5

// ---------- Fit and saving ----------
#define SOH_MAX_SLICES         32
#define SOH_SEARCH_LO          0.2      // search C between 20%...
#define SOH_SEARCH_HI          1.6      // ...and 160% of the label
#define SOH_OUTLIER_SIGMA      3.0
#define SOH_STREAK_RESET       3        // this many outliers in a row, all on the same side: the pack itself changed
#define SOH_HIGH_CONF_PCT      6.0      // plus-or-minus at or under this: confidence "high"
#define SOH_LIVE_FIT_EVERY_S   30       // refit with the open slice at most this often
#define SOH_OPEN_SAVE_EVERY_S  300      // save the open slice at most this often
#define SOH_STATE_SAVE_EVERY_S 1800     // save the cycle count at most this often (it changes slowly)

namespace soh {

// Typical 18650 open-circuit voltage per cell -> charge percent (the generic table the battery estimator uses).
constexpr float kCellV[]   = {4.20f, 4.15f, 4.11f, 4.08f, 4.02f, 3.98f, 3.95f, 3.91f, 3.87f, 3.85f, 3.84f,
                              3.82f, 3.80f, 3.79f, 3.77f, 3.75f, 3.73f, 3.71f, 3.69f, 3.61f, 3.30f, 3.00f};
constexpr float kCellPct[] = { 100,    95,    90,    85,    80,    75,    70,    65,    60,    55,    50,
                                45,    40,    35,    30,    25,    20,    15,    10,     5,     2,     0};
constexpr int   kTableN    = sizeof(kCellV) / sizeof(kCellV[0]);

constexpr size_t kBlobMax  = 600;      // the largest saved blob (sohSlices) fits in this
constexpr int    kRingN    = 72;       // 2 min of 2 s samples, with room to spare

enum { CONF_NONE, CONF_LOW, CONF_HIGH };
enum { LEARN_WAITING, LEARN_DISCHARGING, LEARN_CHARGING };
enum { ANCHOR_FULL, ANCHOR_REST, ANCHOR_CUTOFF, ANCHOR_UNKNOWN = 255 };
enum { PHASE_LOST, PHASE_DISCHARGE, PHASE_CHARGING, PHASE_IDLE };
enum { WHY_OK, WHY_NO_START, WHY_NO_END, WHY_TOO_SMALL, WHY_GAPS, WHY_NO_TEMP, WHY_TOO_COLD, WHY_TOO_HOT,
       WHY_OUTLIER, WHY_OLD_OUTLIER, WHY_PACK_CHANGED, WHY_RESET };
enum { CHECK_OK, CHECK_SETTLING, CHECK_NO_TEMP, CHECK_SHORT, CHECK_NO_CURRENT, CHECK_CURRENT_HIGH,
       CHECK_UNSTEADY, CHECK_RISING, CHECK_FALLING, CHECK_COUNT };
enum { CLOSED_NONE, CLOSED_CHARGER, CLOSED_CUTOFF, CLOSED_RESTART, CLOSED_LOST };
enum { SAVE_STATE = 1, SAVE_SLICES = 2, SAVE_OPEN = 4, REMOVE_OPEN = 8 };

inline const char* confText(int c)   { return c == CONF_HIGH ? "high" : c == CONF_LOW ? "low" : "none"; }
inline const char* learnText(int l)  { return l == LEARN_CHARGING ? "charging" : l == LEARN_DISCHARGING ? "discharging" : "waiting"; }
inline const char* anchorText(int a) { return a == ANCHOR_FULL ? "full" : a == ANCHOR_REST ? "rest" : a == ANCHOR_CUTOFF ? "cutoff" : "none"; }

inline const char* whyText(int w) {
  switch (w) {
    case WHY_NO_START:     return "no start point";
    case WHY_NO_END:       return "no end point";
    case WHY_TOO_SMALL:    return "too small";
    case WHY_GAPS:         return "gaps in the data";
    case WHY_NO_TEMP:      return "no temperature";
    case WHY_TOO_COLD:     return "too cold";
    case WHY_TOO_HOT:      return "too hot";
    case WHY_OUTLIER:      return "outlier";
    case WHY_OLD_OUTLIER:  return "an older outlier slice was dropped";
    case WHY_PACK_CHANGED: return "pack changed: older slices cleared";
    case WHY_RESET:        return "reset";
    default:               return "";
  }
}

inline const char* checkText(int c) {
  switch (c) {
    case CHECK_OK:           return "ok";
    case CHECK_SETTLING:     return "settling after a charge or start";
    case CHECK_NO_TEMP:      return "temperature unknown";
    case CHECK_SHORT:        return "under 2 min of steady data";
    case CHECK_NO_CURRENT:   return "no current reading";
    case CHECK_CURRENT_HIGH: return "current too high";
    case CHECK_UNSTEADY:     return "current not steady";
    case CHECK_RISING:       return "voltage rising";
    case CHECK_FALLING:      return "voltage falling fast";
    default:                 return "";
  }
}

inline const char* closedText(int c) {
  return c == CLOSED_CHARGER ? "charger" : c == CLOSED_CUTOFF ? "cutoff" : c == CLOSED_RESTART ? "restart"
       : c == CLOSED_LOST ? "battery reading lost" : "";
}

// ===================================================================
// VOLTAGE TABLE
// ===================================================================
inline float socFromCellV(float v) {
  if (isnan(v)) return NAN;
  if (v >= kCellV[0]) return 100;
  if (v <= kCellV[kTableN - 1]) return 0;
  for (int i = 0; i < kTableN - 1; i++) {
    if (v >= kCellV[i + 1]) {
      float f = (v - kCellV[i + 1]) / (kCellV[i] - kCellV[i + 1]);
      return kCellPct[i + 1] + f * (kCellPct[i] - kCellPct[i + 1]);
    }
  }
  return 0;
}

// Energy percent (SOE): like the charge percent, but each percent of charge is weighted by its voltage,
// because a percent near full (higher voltage) holds more watt-hours than one near empty.
inline float soeFromCellV(float v) {
  if (isnan(v)) return NAN;
  double below = 0, total = 0;
  bool done = false;
  for (int i = kTableN - 1; i > 0; i--) {             // segments from empty upward
    double vLo = kCellV[i], vHi = kCellV[i - 1], width = kCellPct[i - 1] - kCellPct[i];
    double seg = width * (vLo + vHi) / 2;
    total += seg;
    if (done) continue;
    if (v >= vHi) { below += seg; continue; }
    if (v > vLo) below += width * (v - vLo) / (vHi - vLo) * (vLo + v) / 2;
    done = true;
  }
  return (float)(100.0 * below / total);
}

// ===================================================================
// DATA
// ===================================================================
// One averaged 2 s window from the firmware. NaN means "no reading".
struct Sample {
  float dtS;      // seconds since the previous sample
  float battV;    // battery-side voltage
  float mA;       // load current (the INA219 never sees charger current)
  float mW;       // mean power over the window (NaN: use battV x mA)
  float tempC;    // box air temperature
};

// Running totals since this boot. Anchors keep a snapshot; a slice is the difference of two snapshots.
struct Totals { double t, e, ah, gap, tempSum, tempS; };

struct Anchor {
  Totals  at;
  float   soe, sigma;   // energy percent and its uncertainty, in SOE points
  uint8_t kind;
};

// One piece of evidence: e watt-hours moved the pack x SOE points.
struct Slice { float e, x, sE, sX; uint8_t start; };

struct Fit {
  bool    valid;
  uint8_t n, conf;
  float   capWh, sohPct, sigmaPct;   // sigmaPct includes the floor
};

// One /station/healthLog record. NaN fields are sent as null.
struct Result {
  bool    reset;        // a `z` reset marker (the website's chart breaks its line here)
  bool    ok;
  uint8_t why, detail;  // detail: the anchor check that failed most, for "no start point" and "no end point"
  uint8_t start;        // ANCHOR_FULL, ANCHOR_REST or ANCHOR_UNKNOWN
  float   e, ah, x, sx, sliceSohPct, fitSohPct, fitSigmaPct, tempC, hours;
};

// What /station/health shows. NaN fields are sent as null.
struct Status {
  float   sohPct, sigmaPct, capWh, cycles, progressPct;
  uint8_t conf, learn, slices;
};

// The numbers a pair of anchors gives.
struct Span { double e, ah, x, sx, sE, hours, gapFrac, tempC; bool hasTemp; };

inline Span spanOf(const Anchor& a, const Anchor& b) {
  Span s;
  s.e = b.at.e - a.at.e;
  s.ah = b.at.ah - a.at.ah;
  s.x = (double)a.soe - b.soe;
  s.sx = sqrt((double)a.sigma * a.sigma + (double)b.sigma * b.sigma);
  s.sE = s.e * SOH_E_SIGMA_PCT / 100.0 + SOH_E_SIGMA_WH;
  double dur = b.at.t - a.at.t;
  s.hours = dur / 3600.0;
  s.gapFrac = dur > 0 ? (b.at.gap - a.at.gap) / dur : 0;
  double tS = b.at.tempS - a.at.tempS;
  s.hasTemp = dur > 0 && tS >= 0.5 * (dur - (b.at.gap - a.at.gap));
  s.tempC = tS > 0 ? (b.at.tempSum - a.at.tempSum) / tS : NAN;
  return s;
}

inline double relErr(const Span& s) {
  if (!(s.e > 0) || !(s.x > 0)) return INFINITY;
  return sqrt((s.sE / s.e) * (s.sE / s.e) + (s.sx / s.x) * (s.sx / s.x));
}

inline uint8_t judge(const Span& s) {
  if (s.x < SOH_MIN_X || s.e < SOH_MIN_E_WH) return WHY_TOO_SMALL;
  if (s.gapFrac > SOH_GAP_MAX_FRAC)          return WHY_GAPS;
  if (!s.hasTemp)                            return WHY_NO_TEMP;
  if (s.tempC < SOH_TEMP_MIN_C)              return WHY_TOO_COLD;
  if (s.tempC > SOH_TEMP_MAX_C)              return WHY_TOO_HOT;
  return WHY_OK;
}

inline Slice sliceOf(const Span& s, uint8_t start) {
  Slice sl = {(float)s.e, (float)s.x, (float)s.sE, (float)s.sx, start};
  return sl;
}

// ===================================================================
// FIT (weighted total least squares, Plett 2011)
// ===================================================================
// Both e and x carry errors. The capacity C (Wh at 100 SOE points) that best explains every slice minimizes
//   J(C) = sum (e_i - C x_i / 100)^2 / (sE_i^2 + (C / 100)^2 sX_i^2)
inline double costAt(const Slice* s, int n, double C) {
  double c = C / 100.0, J = 0;
  for (int i = 0; i < n; i++) {
    double r = s[i].e - c * s[i].x;
    J += r * r / ((double)s[i].sE * s[i].sE + c * c * s[i].sX * s[i].sX);
  }
  return J;
}

inline double normResidual(const Slice& s, double C) {
  double c = C / 100.0;
  return (s.e - c * s.x) / sqrt((double)s.sE * s.sE + c * c * s.sX * s.sX);
}

// Index of the slice furthest from the fit, if it is past the outlier line; otherwise -1.
// Needs 3 slices: with 2, there is no telling which one is wrong.
inline int outlierIndex(const Slice* s, int n, double C, double* rOut = nullptr) {
  if (n < 3) return -1;
  int worst = -1;
  double worstR = 0;
  for (int i = 0; i < n; i++) {
    double r = normResidual(s[i], C);
    if (fabs(r) > fabs(worstR)) { worstR = r; worst = i; }
  }
  if (rOut) *rOut = worstR;
  return fabs(worstR) > SOH_OUTLIER_SIGMA ? worst : -1;
}

inline Fit fitSlices(const Slice* s, int n) {
  Fit f = {false, 0, CONF_NONE, NAN, NAN, NAN};
  if (n <= 0) return f;
  // J can have more than one dip when slices disagree, so scan a grid first, then refine the best dip
  // with a golden-section search.
  const double lo = SOH_SEARCH_LO * SOH_RATED_WH, hi = SOH_SEARCH_HI * SOH_RATED_WH;
  const int steps = 140;
  const double step = (hi - lo) / steps;
  int best = 0;
  double bestJ = INFINITY;
  for (int k = 0; k <= steps; k++) {
    double J = costAt(s, n, lo + k * step);
    if (J < bestJ) { bestJ = J; best = k; }
  }
  double a = lo + (best > 0 ? best - 1 : 0) * step, b = lo + (best < steps ? best + 1 : steps) * step;
  const double g = 0.6180339887498949;
  double c1 = b - g * (b - a), c2 = a + g * (b - a), J1 = costAt(s, n, c1), J2 = costAt(s, n, c2);
  for (int it = 0; it < 80 && b - a > 1e-7; it++) {
    if (J1 < J2) { b = c2; c2 = c1; J2 = J1; c1 = b - g * (b - a); J1 = costAt(s, n, c1); }
    else         { a = c1; c1 = c2; J1 = J2; c2 = a + g * (b - a); J2 = costAt(s, n, c2); }
  }
  double C = (a + b) / 2, Jmin = costAt(s, n, C);

  // Uncertainty from the curvature at the best C: sigma_C^2 = 2 / J''.
  double h = 1e-3 * C;
  double d2 = (costAt(s, n, C + h) - 2 * Jmin + costAt(s, n, C - h)) / (h * h);
  double sigC = d2 > 0 ? sqrt(2.0 / d2) : SOH_RATED_WH;
  // If the slices disagree more than their own uncertainties allow, widen it by that much.
  if (n >= 2 && Jmin / (n - 1) > 1) sigC *= sqrt(Jmin / (n - 1));
  double soh = C / SOH_RATED_WH * 100.0;
  double stat = sigC / SOH_RATED_WH * 100.0;

  // More slices cannot average away an error that repeats in every slice: the INA219 gain, and the voltage
  // table at the anchors (the same pack and curve each time). So the plus-or-minus never drops below what
  // the best single slice allows from those two parts. For one slice this changes nothing.
  double bestRelX = INFINITY;
  for (int i = 0; i < n; i++) if (s[i].x > 0) bestRelX = fmin(bestRelX, (double)s[i].sX / s[i].x);
  double sys = soh * sqrt(SOH_E_SIGMA_PCT / 100.0 * SOH_E_SIGMA_PCT / 100.0 + bestRelX * bestRelX);
  double part = fmax(stat, sys);

  f.valid = true;
  f.n = (uint8_t)n;
  f.capWh = (float)C;
  f.sohPct = (float)soh;
  f.sigmaPct = (float)sqrt(part * part + SOH_FLOOR_PCT * SOH_FLOOR_PCT);
  f.conf = f.sigmaPct <= SOH_HIGH_CONF_PCT ? CONF_HIGH : CONF_LOW;
  return f;
}

// ===================================================================
// SAVING (Preferences blobs: version byte, fields, CRC32)
// ===================================================================
inline uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

struct Writer {
  uint8_t* p; size_t cap, n; bool ok;
  Writer(uint8_t* buf, size_t c) : p(buf), cap(c), n(0), ok(true) {}
  void raw(const void* src, size_t len) {
    if (!ok || n + len > cap) { ok = false; return; }
    memcpy(p + n, src, len); n += len;
  }
  template <class T> void put(T v) { raw(&v, sizeof v); }
  size_t finish() {
    uint32_t c = crc32(p, n);
    raw(&c, sizeof c);
    return ok ? n : 0;
  }
};

struct Reader {
  const uint8_t* p; size_t len, n; bool ok;
  // Checks the CRC and version up front; every get() after a failure returns zeros.
  Reader(const uint8_t* buf, size_t l, uint8_t version) : p(buf), len(l), n(0), ok(false) {
    if (!buf || l < 5) return;
    uint32_t c;
    memcpy(&c, buf + l - 4, 4);
    ok = c == crc32(buf, l - 4) && buf[0] == version;
    len = l - 4;
    n = 1;
  }
  void raw(void* dst, size_t l) {
    if (!ok || n + l > len) { ok = false; memset(dst, 0, l); return; }
    memcpy(dst, p + n, l); n += l;
  }
  template <class T> T get() { T v; raw(&v, sizeof v); return v; }
  bool done() const { return ok && n == len; }
};

inline void putSlice(Writer& w, const Slice& s) { w.put(s.e); w.put(s.x); w.put(s.sE); w.put(s.sX); w.put(s.start); }
inline Slice getSlice(Reader& r) {
  Slice s;
  s.e = r.get<float>(); s.x = r.get<float>(); s.sE = r.get<float>(); s.sX = r.get<float>(); s.start = r.get<uint8_t>();
  return s;
}
inline void putAnchor(Writer& w, const Anchor& a) {
  w.put(a.at.t); w.put(a.at.e); w.put(a.at.ah); w.put(a.at.gap); w.put(a.at.tempSum); w.put(a.at.tempS);
  w.put(a.soe); w.put(a.sigma); w.put(a.kind);
}
inline Anchor getAnchor(Reader& r) {
  Anchor a;
  a.at.t = r.get<double>(); a.at.e = r.get<double>(); a.at.ah = r.get<double>();
  a.at.gap = r.get<double>(); a.at.tempSum = r.get<double>(); a.at.tempS = r.get<double>();
  a.soe = r.get<float>(); a.sigma = r.get<float>(); a.kind = r.get<uint8_t>();
  return a;
}
inline void putResult(Writer& w, const Result& x) {
  w.put(x.reset); w.put(x.ok); w.put(x.why); w.put(x.detail); w.put(x.start);
  w.put(x.e); w.put(x.ah); w.put(x.x); w.put(x.sx); w.put(x.sliceSohPct);
  w.put(x.fitSohPct); w.put(x.fitSigmaPct); w.put(x.tempC); w.put(x.hours);
}
inline Result getResult(Reader& r) {
  Result x;
  x.reset = r.get<bool>(); x.ok = r.get<bool>(); x.why = r.get<uint8_t>(); x.detail = r.get<uint8_t>();
  x.start = r.get<uint8_t>();
  x.e = r.get<float>(); x.ah = r.get<float>(); x.x = r.get<float>(); x.sx = r.get<float>();
  x.sliceSohPct = r.get<float>(); x.fitSohPct = r.get<float>(); x.fitSigmaPct = r.get<float>();
  x.tempC = r.get<float>(); x.hours = r.get<float>();
  return x;
}

constexpr uint8_t kStateVer = 1, kSlicesVer = 1, kOpenVer = 1;

// ===================================================================
// ESTIMATOR
// ===================================================================
struct Estimator {
  // Evidence and the fit over it
  Slice   slices[SOH_MAX_SLICES];        // oldest first
  uint8_t nSlices;
  Slice   streak[SOH_STREAK_RESET];      // recent outliers that agree with each other
  uint8_t nStreak;
  int8_t  streakSide;
  Fit     fit;                           // over the stored slices
  Fit     live;                          // stored slices plus the open one when it is usable: what the website shows
  bool    liveHasOpen;
  double  cycleWh;                       // discharged energy for the cycle count, charging excluded

  // The last closed slice, waiting to be appended to /station/healthLog
  Result  result;
  bool    resultPending;
  uint32_t resultCount;                  // goes up with every new result (not saved), so the caller can spot one

  Totals  tot;

  // Learner
  uint8_t phase;
  double  settleStartT;
  bool    hasStart, hasEnd;
  Anchor  start, end;
  double  endRel;                        // relative error of the slice start..end
  uint16_t failCount[CHECK_COUNT];       // why anchors were refused since the last one, for the log's reason
  uint8_t lastCheck, lastClosed;

  // Charger detector and the other timers
  double  ema, minEma, maxEma, riseS, dropS, fullSinceT, heldFullS, cutoffS, invalidS;
  bool    heldValid;
  Anchor  held;                          // the last moment the charger held the pack full

  // 2 minute window for the quasi-rest check
  struct Point { double t; float voc, mA; };
  Point   ring[kRingN];
  uint8_t ringHead, ringCount;

  // Saving
  uint8_t saves;
  bool    openDirty, liveDirty;
  double  lastOpenSaveT, lastStateSaveT, savedCycleWh, lastLiveFitT;

  Estimator() { clearAll(); }

  void clearAll() {
    memset((void*)this, 0, sizeof *this);   // every member is plain data
    fit = live = fitSlices(nullptr, 0);
    phase = PHASE_LOST;
    lastOpenSaveT = lastStateSaveT = lastLiveFitT = -1e9;
    fullSinceT = -1;
    result.start = ANCHOR_UNKNOWN;
  }

  // The `z` command: forget everything, and log a marker so the chart breaks its line.
  void resetAll() {
    uint32_t count = resultCount;
    clearAll();
    resultCount = count;
    Result r = blankResult();
    r.reset = true;
    r.why = WHY_RESET;
    post(r);
    saves = SAVE_STATE | SAVE_SLICES | REMOVE_OPEN;
  }

  // Call after the health log record reached Firebase (the flag is saved, so a restart does not send it twice).
  void markPosted() {
    resultPending = false;
    saves |= SAVE_STATE;
  }

  uint8_t takeSaves() {
    uint8_t f = saves;
    saves = 0;
    if (f & SAVE_STATE) { savedCycleWh = cycleWh; lastStateSaveT = tot.t; }
    if (f & SAVE_OPEN) lastOpenSaveT = tot.t;
    return f;
  }

  // ---------- One sample (every 2 s) ----------
  void step(const Sample& s) {
    double dt = s.dtS;
    if (!(dt > 0)) return;
    double used = dt > SOH_DT_CAP_S ? SOH_DT_CAP_S : dt;
    tot.t += dt;

    bool valid = isfinite(s.battV) && isfinite(s.mA) && s.battV >= SOH_V_VALID_MIN && s.battV <= SOH_V_VALID_MAX;
    if (!valid) {
      tot.gap += dt;
      ringCount = 0;
      invalidS += dt;
      if (phase != PHASE_LOST && invalidS >= SOH_LOST_S) {
        closeStretch(CLOSED_LOST);
        phase = PHASE_LOST;
      }
      return;
    }
    invalidS = 0;
    if (dt > used) { tot.gap += dt - used; ringCount = 0; }

    double mW = isfinite(s.mW) ? s.mW : (double)s.battV * s.mA;
    double dWh = mW / 1000.0 * used / 3600.0;
    tot.e += dWh;
    tot.ah += s.mA / 1000.0 * used / 3600.0;
    if (isfinite(s.tempC)) { tot.tempSum += s.tempC * used; tot.tempS += used; }
    if (phase != PHASE_CHARGING && dWh > 0) cycleWh += dWh;

    double voc = s.battV + s.mA / 1000.0 * SOH_R_INT_OHM;
    if (phase == PHASE_LOST) ema = voc;
    else ema += (1 - exp(-dt / SOH_SMOOTH_S)) * (voc - ema);
    ringPush(voc, s.mA);

    if (phase == PHASE_LOST) {
      // First reading after a start: a pack at full voltage may still be on the charger.
      if (voc >= SOH_FULL_V) beginCharging(); else beginStretch();
    }
    switch (phase) {
      case PHASE_CHARGING:  stepCharging(dt); break;
      case PHASE_DISCHARGE: stepDischarge(s, dt, voc); break;
      case PHASE_IDLE:      watchForCharger(dt); break;
    }

    if (liveDirty && tot.t - lastLiveFitT >= SOH_LIVE_FIT_EVERY_S) updateLive();
    if (openDirty && phase == PHASE_DISCHARGE && hasStart && tot.t - lastOpenSaveT >= SOH_OPEN_SAVE_EVERY_S) {
      saves |= SAVE_OPEN;
      openDirty = false;
    }
    if (cycleWh - savedCycleWh >= 0.05 && tot.t - lastStateSaveT >= SOH_STATE_SAVE_EVERY_S) saves |= SAVE_STATE;
  }

  // ---------- What the website shows ----------
  Status status() const {
    Status st;
    st.conf = live.valid ? live.conf : (uint8_t)CONF_NONE;
    st.sohPct = live.sohPct;
    st.sigmaPct = live.sigmaPct;
    st.capWh = live.capWh;
    st.slices = live.valid ? live.n : 0;
    st.cycles = (float)(cycleWh / SOH_RATED_WH);
    st.learn = phase == PHASE_CHARGING ? LEARN_CHARGING
             : (phase == PHASE_DISCHARGE && hasStart) ? LEARN_DISCHARGING : LEARN_WAITING;
    st.progressPct = progress();
    return st;
  }

  // Half for finding a start point (the settle time), half for enough discharge after it.
  float progress() const {
    if (phase != PHASE_DISCHARGE) return 0;
    if (!hasStart) {
      double f = (tot.t - settleStartT) / SOH_SETTLE_S;
      return (float)(50 * (f < 1 ? f : 1));
    }
    if (!hasEnd) return 50;
    Span sp = spanOf(start, end);
    double f = fmin(sp.x / SOH_MIN_X, sp.e / SOH_MIN_E_WH);
    return (float)(50 + 50 * fmax(0, fmin(1, f)));
  }

  // ---------- Saving ----------
  size_t saveSlices(uint8_t* buf, size_t cap) const {
    Writer w(buf, cap);
    w.put(kSlicesVer);
    w.put(nSlices);
    for (int i = 0; i < nSlices; i++) putSlice(w, slices[i]);
    return w.finish();
  }

  size_t saveState(uint8_t* buf, size_t cap) const {
    Writer w(buf, cap);
    w.put(kStateVer);
    w.put(cycleWh);
    w.put(nStreak);
    w.put(streakSide);
    for (int i = 0; i < nStreak; i++) putSlice(w, streak[i]);
    putResult(w, result);
    w.put(resultPending);
    return w.finish();
  }

  size_t saveOpen(uint8_t* buf, size_t cap) const {
    Writer w(buf, cap);
    w.put(kOpenVer);
    putAnchor(w, start);
    w.put(hasEnd);
    putAnchor(w, end);
    for (int i = 0; i < CHECK_COUNT; i++) w.put(failCount[i]);
    return w.finish();
  }

  // Load in this order after a start: slices, state, open. A missing or corrupt blob starts that part empty.
  bool loadSlices(const uint8_t* buf, size_t len) {
    Reader r(buf, len, kSlicesVer);
    uint8_t n = r.get<uint8_t>();
    Slice tmp[SOH_MAX_SLICES];
    if (n > SOH_MAX_SLICES) r.ok = false;
    for (int i = 0; r.ok && i < n; i++) tmp[i] = getSlice(r);
    bool good = r.done();
    nSlices = good ? n : 0;
    if (good) memcpy(slices, tmp, n * sizeof(Slice));
    refit();
    return good;
  }

  bool loadState(const uint8_t* buf, size_t len) {
    Reader r(buf, len, kStateVer);
    double cw = r.get<double>();
    uint8_t ns = r.get<uint8_t>();
    int8_t side = r.get<int8_t>();
    Slice tmp[SOH_STREAK_RESET];
    if (ns > SOH_STREAK_RESET) r.ok = false;
    for (int i = 0; r.ok && i < ns; i++) tmp[i] = getSlice(r);
    Result res = getResult(r);
    bool pend = r.get<bool>();
    if (!r.done()) return false;
    cycleWh = savedCycleWh = cw;
    nStreak = ns;
    streakSide = side;
    memcpy(streak, tmp, ns * sizeof(Slice));
    result = res;
    resultPending = pend;
    return true;
  }

  // A start ends the stretch that was open, at its last saved end point (the station may have been
  // charged while it was off, so the slice cannot simply carry on).
  bool loadOpen(const uint8_t* buf, size_t len) {
    Reader r(buf, len, kOpenVer);
    Anchor st = getAnchor(r);
    bool he = r.get<bool>();
    Anchor en = getAnchor(r);
    uint16_t fails[CHECK_COUNT];
    for (int i = 0; i < CHECK_COUNT; i++) fails[i] = r.get<uint16_t>();
    bool good = r.done();
    if (good) {
      finishSlice(st, he, en, fails);
      lastClosed = CLOSED_RESTART;
    }
    saves |= REMOVE_OPEN;
    updateLive();
    return good;
  }

  // ---------- Debug (serial `h`) ----------
  typedef void (*LineFn)(const char* line, void* ctx);

  void debugLines(LineFn out, void* ctx) const {
    char b[160];
    Status st = status();
    if (live.valid) {
      snprintf(b, sizeof b, "SOH %.1f%% +-%.1f (%s), usable %.2f Wh of %.1f rated, %d slices%s",
               st.sohPct, st.sigmaPct, confText(st.conf), st.capWh, SOH_RATED_WH, st.slices,
               liveHasOpen ? " (incl. the open one)" : "");
    } else {
      snprintf(b, sizeof b, "SOH --: no usable slice yet (learning %.0f%%)", st.progressPct);
    }
    out(b, ctx);
    snprintf(b, sizeof b, "Cycles %.2f (%.2f Wh discharged), learner %s", st.cycles, cycleWh, learnText(st.learn));
    out(b, ctx);
    for (int i = 0; i < nSlices; i++) {
      const Slice& s = slices[i];
      snprintf(b, sizeof b, "  slice %2d: %6.2f Wh over %5.1f +-%4.1f pts -> %5.1f%% (%s)", i + 1, s.e, s.x, s.sX,
               s.x > 0 ? s.e / s.x * 100 / SOH_RATED_WH * 100 : NAN, anchorText(s.start));
      out(b, ctx);
    }
    if (nStreak) {
      snprintf(b, sizeof b, "  %d recent outlier(s) on the %s side", nStreak, streakSide > 0 ? "high" : "low");
      out(b, ctx);
    }
    const char* ph = phase == PHASE_CHARGING ? "charging" : phase == PHASE_DISCHARGE ? "discharging"
                   : phase == PHASE_IDLE ? "empty, waiting for a charge" : "no battery reading";
    snprintf(b, sizeof b, "Now: %s, V_oc %.3f V (smoothed)", ph, ema);
    out(b, ctx);
    if (phase == PHASE_CHARGING) {
      snprintf(b, sizeof b, "  held full %.0f of %d min needed", heldValid ? heldFullS / 60 : 0.0, SOH_FULL_HOLD_S / 60);
      out(b, ctx);
    }
    if (phase == PHASE_DISCHARGE) {
      if (hasStart) {
        snprintf(b, sizeof b, "  open slice: start %s at %.1f%% +-%.1f, %.1f h ago", anchorText(start.kind),
                 start.soe, start.sigma, (tot.t - start.at.t) / 3600);
        out(b, ctx);
      }
      if (hasStart && hasEnd) {
        Span sp = spanOf(start, end);
        snprintf(b, sizeof b, "  best end %.1f%% +-%.1f: %.2f Wh over %.1f pts, +-%.0f%% (%s)", end.soe, end.sigma,
                 sp.e, sp.x, endRel * 100, judge(sp) == WHY_OK ? "usable" : whyText(judge(sp)));
        out(b, ctx);
      }
      snprintf(b, sizeof b, "  anchor check now: %s", checkText(lastCheck));
      out(b, ctx);
    }
    if (result.reset) {
      out("Last result: reset", ctx);
    } else if (result.ok || result.why) {
      snprintf(b, sizeof b, "Last result: %s%s%s", result.ok ? "accepted" : "rejected", result.why ? ", " : "",
               whyText(result.why));
      out(b, ctx);
    }
    if (lastClosed) {
      snprintf(b, sizeof b, "Last stretch ended by: %s", closedText(lastClosed));
      out(b, ctx);
    }
  }

  // ===================================================================
  // Internals
  // ===================================================================
  static Result blankResult() {
    Result r;
    r.reset = false; r.ok = false; r.why = WHY_OK; r.detail = CHECK_OK; r.start = ANCHOR_UNKNOWN;
    r.e = r.ah = r.x = r.sx = r.sliceSohPct = r.fitSohPct = r.fitSigmaPct = r.tempC = r.hours = NAN;
    return r;
  }

  void post(const Result& r) { result = r; resultPending = true; resultCount++; }

  void ringPush(double voc, double mA) {
    ring[ringHead].t = tot.t;
    ring[ringHead].voc = (float)voc;
    ring[ringHead].mA = (float)mA;
    ringHead = (uint8_t)((ringHead + 1) % kRingN);
    if (ringCount < kRingN) ringCount++;
  }

  void beginStretch() {
    phase = PHASE_DISCHARGE;
    settleStartT = tot.t;
    hasStart = hasEnd = false;
    minEma = ema;
    riseS = cutoffS = 0;
    memset(failCount, 0, sizeof failCount);
    openDirty = false;
  }

  void beginCharging() {
    phase = PHASE_CHARGING;
    maxEma = ema;
    dropS = 0;
    fullSinceT = -1;
    heldValid = false;
    heldFullS = 0;
  }

  void stepCharging(double dt) {
    if (ema > maxEma) maxEma = ema;
    if (ema >= SOH_FULL_V) {
      if (fullSinceT < 0) fullSinceT = tot.t;
      if (ema >= maxEma - SOH_HOLD_TOL_V) {
        held.at = tot;
        heldValid = true;
        heldFullS = tot.t - fullSinceT;
      }
    } else {
      fullSinceT = -1;
    }
    bool gone = ema < maxEma - SOH_CHARGE_DROP_V || (maxEma >= SOH_FULL_V && ema < SOH_FULL_V);
    dropS = gone ? dropS + dt : 0;
    if (dropS < SOH_CHARGE_HOLD_S) return;

    // The charger is gone. If it held the pack full long enough, the moment it let go is a full anchor.
    bool full = heldValid && heldFullS >= SOH_FULL_HOLD_S;
    beginStretch();
    if (full) {
      start = held;
      start.kind = ANCHOR_FULL;
      start.soe = 100;
      start.sigma = SOH_FULL_SIGMA;
      hasStart = true;
      openDirty = true;
    }
  }

  // Charger plugged in: the smoothed V_oc climbs well above the lowest it reached in this stretch.
  bool watchForCharger(double dt) {
    if (ema < minEma) minEma = ema;
    riseS = ema > minEma + SOH_CHARGE_RISE_V ? riseS + dt : 0;
    if (riseS < SOH_CHARGE_HOLD_S) return false;
    if (phase == PHASE_DISCHARGE) closeStretch(CLOSED_CHARGER);
    beginCharging();
    return true;
  }

  void stepDischarge(const Sample& s, double dt, double voc) {
    if (watchForCharger(dt)) return;

    cutoffS = s.battV <= SOH_CUTOFF_V ? cutoffS + dt : 0;
    if (cutoffS >= SOH_CUTOFF_HOLD_S) {
      if (hasStart) considerEnd(anchorAt(ANCHOR_CUTOFF, voc, s.mA));
      closeStretch(CLOSED_CUTOFF);
      phase = PHASE_IDLE;            // empty: nothing more to learn until a charge
      minEma = ema;
      riseS = 0;
      return;
    }

    double vocNow = 0, mAmean = 0;
    lastCheck = restCheck(s, vocNow, mAmean);
    if (lastCheck == CHECK_OK) {
      Anchor a = anchorAt(ANCHOR_REST, vocNow, mAmean);
      if (!hasStart) {
        start = a;
        hasStart = true;
        openDirty = true;
        memset(failCount, 0, sizeof failCount);
      } else {
        considerEnd(a);
      }
    } else if (lastCheck != CHECK_SETTLING && failCount[lastCheck] < 0xFFFF) {
      failCount[lastCheck]++;
    }
  }

  // Quasi-rest: small, steady current and a settled voltage over the last 2 minutes.
  uint8_t restCheck(const Sample& s, double& vocNow, double& mAmean) const {
    if (tot.t - settleStartT < SOH_SETTLE_S) return CHECK_SETTLING;
    if (!isfinite(s.tempC)) return CHECK_NO_TEMP;
    const double from = tot.t - SOH_STEADY_S - 0.5;
    int n = 0;
    double sumI = 0, oldest = tot.t;
    for (int k = 0; k < ringCount; k++) {
      const Point& p = ring[(ringHead + kRingN - 1 - k) % kRingN];
      if (p.t < from) break;
      sumI += p.mA;
      oldest = p.t;
      n++;
    }
    if (n < 10 || tot.t - oldest < SOH_STEADY_S - 2.5) return CHECK_SHORT;
    double mean = sumI / n;
    if (!(mean > 1)) return CHECK_NO_CURRENT;
    if (mean >= SOH_REST_MAX_MA) return CHECK_CURRENT_HIGH;
    double tol = fmax(mean * SOH_STEADY_PCT / 100.0, SOH_STEADY_MA);
    double st = 0, sv = 0, stt = 0, stv = 0;
    for (int k = 0; k < n; k++) {
      const Point& p = ring[(ringHead + kRingN - 1 - k) % kRingN];
      if (fabs(p.mA - mean) > tol) return CHECK_UNSTEADY;
      double x = p.t - tot.t;
      st += x; sv += p.voc; stt += x * x; stv += x * p.voc;
    }
    double den = n * stt - st * st;
    double slope = den > 0 ? (n * stv - st * sv) / den : 0;   // volts per second
    double mvPerMin = slope * 60000.0;
    if (mvPerMin > SOH_RISE_MAX_MV_MIN) return CHECK_RISING;
    if (mvPerMin < -SOH_FALL_MAX_MV_MIN) return CHECK_FALLING;
    vocNow = (sv - slope * st) / n;     // the fitted line at this moment (smoother than one sample)
    mAmean = mean;
    return CHECK_OK;
  }

  // SOE from the table, and how far off it could be: the table error plus the resistance guess,
  // turned into SOE points through the local slope of the curve.
  Anchor anchorAt(uint8_t kind, double vocPack, double mA) const {
    Anchor a;
    a.at = tot;
    a.kind = kind;
    double v = vocPack / SOH_CELLS;
    double rV = mA / 1000.0 * SOH_RINT_SIGMA_OHM / SOH_CELLS;
    double sV = sqrt(SOH_TABLE_SIGMA_MV / 1000.0 * SOH_TABLE_SIGMA_MV / 1000.0 + rV * rV);
    a.soe = soeFromCellV((float)v);
    double sig = fabs(soeFromCellV((float)(v + sV)) - soeFromCellV((float)(v - sV))) / 2;
    a.sigma = (float)fmax(sig, SOH_ANCHOR_MIN_SIGMA);
    return a;
  }

  // Keep the end point that gives the smallest relative error (usually the latest).
  void considerEnd(const Anchor& a) {
    double re = relErr(spanOf(start, a));
    if (!isfinite(re) || (hasEnd && re >= endRel)) return;
    end = a;
    endRel = re;
    hasEnd = true;
    openDirty = true;
    liveDirty = true;
  }

  uint8_t dominantCheck(const uint16_t* fails) const {
    int best = CHECK_OK;
    for (int i = 1; i < CHECK_COUNT; i++) if (fails[i] > fails[best]) best = i;
    return (uint8_t)best;
  }

  void closeStretch(uint8_t why) {
    if (phase != PHASE_DISCHARGE) return;
    lastClosed = why;
    if (hasStart) {
      finishSlice(start, hasEnd, end, failCount);
    } else if (tot.t - settleStartT >= SOH_SETTLE_S) {
      // It had a fair chance to find a start point and did not: log why, so the checks can be tuned.
      Result r = blankResult();
      r.why = WHY_NO_START;
      r.detail = dominantCheck(failCount);
      r.hours = (float)((tot.t - settleStartT) / 3600.0);
      fillFit(r);
      post(r);
    }
    hasStart = hasEnd = false;
    openDirty = false;
    saves |= SAVE_STATE | REMOVE_OPEN;
    updateLive();
  }

  void finishSlice(const Anchor& st, bool he, const Anchor& en, const uint16_t* fails) {
    Result r = blankResult();
    r.start = st.kind;
    if (!he) {
      r.why = WHY_NO_END;
      r.detail = dominantCheck(fails);
    } else {
      Span sp = spanOf(st, en);
      r.e = (float)sp.e; r.ah = (float)sp.ah; r.x = (float)sp.x; r.sx = (float)sp.sx;
      r.sliceSohPct = (float)(sp.x > 0 ? sp.e / sp.x * 100.0 / SOH_RATED_WH * 100.0 : NAN);
      r.tempC = (float)sp.tempC;
      r.hours = (float)sp.hours;
      r.why = judge(sp);
      if (r.why == WHY_OK) addSlice(sliceOf(sp, st.kind), r);
    }
    fillFit(r);
    post(r);
    saves |= SAVE_STATE;
  }

  void fillFit(Result& r) const {
    r.fitSohPct = fit.valid ? fit.sohPct : NAN;
    r.fitSigmaPct = fit.valid ? fit.sigmaPct : NAN;
  }

  void pushSlice(const Slice& s) {
    if (nSlices == SOH_MAX_SLICES) {
      memmove(slices, slices + 1, (SOH_MAX_SLICES - 1) * sizeof(Slice));
      nSlices--;
    }
    slices[nSlices++] = s;
  }

  void refit() { fit = fitSlices(slices, nSlices); }

  // Add an accepted slice, with the outlier check: fit everything, and if one slice sits more than
  // 3 of its own sigmas away, drop it and fit again once.
  void addSlice(const Slice& s, Result& r) {
    Slice all[SOH_MAX_SLICES + 1];
    memcpy(all, slices, nSlices * sizeof(Slice));
    all[nSlices] = s;
    int n = nSlices + 1;
    double worstR = 0;
    int worst = outlierIndex(all, n, fitSlices(all, n).capWh, &worstR);

    if (worst == n - 1) {
      // The new slice disagrees with the stored ones. If several in a row disagree the same way,
      // it is the pack that changed (a failing cell, say), not the measurement.
      int8_t side = worstR > 0 ? 1 : -1;
      if (nStreak > 0 && side != streakSide) nStreak = 0;
      streakSide = side;
      streak[nStreak++] = s;
      if (nStreak >= SOH_STREAK_RESET) {
        nSlices = 0;
        for (int i = 0; i < nStreak; i++) pushSlice(streak[i]);
        nStreak = 0;
        r.ok = true;
        r.why = WHY_PACK_CHANGED;
      } else {
        r.ok = false;
        r.why = WHY_OUTLIER;
      }
    } else {
      nStreak = 0;
      if (worst >= 0) {
        memmove(slices + worst, slices + worst + 1, (nSlices - worst - 1) * sizeof(Slice));
        nSlices--;
        r.why = WHY_OLD_OUTLIER;
      }
      pushSlice(s);
      r.ok = true;
    }
    refit();
    saves |= SAVE_SLICES | SAVE_STATE;
  }

  // The website's number: the stored fit, improved by the open slice once it is usable.
  void updateLive() {
    live = fit;
    liveHasOpen = false;
    if (phase == PHASE_DISCHARGE && hasStart && hasEnd) {
      Span sp = spanOf(start, end);
      if (judge(sp) == WHY_OK) {
        Slice all[SOH_MAX_SLICES + 1];
        memcpy(all, slices, nSlices * sizeof(Slice));
        all[nSlices] = sliceOf(sp, start.kind);
        int n = nSlices + 1;
        Fit f = fitSlices(all, n);
        if (outlierIndex(all, n, f.capWh) != n - 1) {
          live = f;
          liveHasOpen = true;
        }
      }
    }
    lastLiveFitT = tot.t;
    liveDirty = false;
  }
};

// ===================================================================
// JSON for Firebase (server timestamp; NaN becomes null)
// ===================================================================
struct Json {
  char* p; size_t cap, n;
  Json(char* buf, size_t c) : p(buf), cap(c), n(0) { if (cap) p[0] = 0; }
  void raw(const char* s) {
    int k = snprintf(p + (n < cap ? n : cap), n < cap ? cap - n : 0, "%s", s);
    if (k > 0) n += (size_t)k;
  }
  void key(const char* k) { raw(n > 1 ? ",\"" : "\""); raw(k); raw("\":"); }
  void num(const char* k, double v, int dec) {
    key(k);
    if (!isfinite(v)) { raw("null"); return; }
    char b[24];
    snprintf(b, sizeof b, "%.*f", dec, v);
    raw(b);
  }
  void str(const char* k, const char* v) {
    key(k);
    if (!v || !*v) { raw("null"); return; }
    raw("\""); raw(v); raw("\"");
  }
  void flag(const char* k, bool v) { key(k); raw(v ? "true" : "false"); }
  int finish() { raw("}"); return n < cap ? (int)n : -1; }
};

// /station/health. Returns the length, or -1 if the buffer is too small.
inline int healthJson(const Status& s, bool inaCalibrated, char* out, size_t cap) {
  Json j(out, cap);
  j.raw("{\"ts\":{\".sv\":\"timestamp\"}");
  bool has = s.conf != CONF_NONE;
  j.num("battSohPct", has ? s.sohPct : NAN, 1);
  j.num("battSohSigmaPct", has ? s.sigmaPct : NAN, 1);
  j.str("battSohConf", confText(s.conf));
  j.num("battCapWhEst", has ? s.capWh : NAN, 2);
  j.num("battCycles", s.cycles, 2);
  j.num("battSohSlices", s.slices, 0);
  j.num("battSohProgressPct", s.progressPct, 0);
  j.str("battSohLearn", learnText(s.learn));
  j.flag("inaCal", inaCalibrated);
  return j.finish();
}

// One /station/healthLog record.
inline int resultJson(const Result& r, char* out, size_t cap) {
  char why[96] = "";
  if (r.why) {
    if (r.detail && (r.why == WHY_NO_START || r.why == WHY_NO_END))
      snprintf(why, sizeof why, "%s: %s", whyText(r.why), checkText(r.detail));
    else
      snprintf(why, sizeof why, "%s", whyText(r.why));
  }
  Json j(out, cap);
  j.raw("{\"ts\":{\".sv\":\"timestamp\"}");
  j.flag("ok", r.ok);
  j.str("why", why);
  j.str("start", r.start == ANCHOR_FULL ? "full" : r.start == ANCHOR_REST ? "rest" : nullptr);
  j.num("e", r.e, 3);
  j.num("ah", r.ah, 3);
  j.num("x", r.x, 2);
  j.num("sx", r.sx, 2);
  j.num("sliceSohPct", r.sliceSohPct, 1);
  j.num("fitSohPct", r.fitSohPct, 1);
  j.num("fitSigmaPct", r.fitSigmaPct, 1);
  j.num("tempC", r.tempC, 1);
  j.num("hours", r.hours, 2);
  return j.finish();
}

}  // namespace soh
