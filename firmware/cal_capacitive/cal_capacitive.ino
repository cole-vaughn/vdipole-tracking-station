/*
 * Capacitive touch pad calibration for the Satellite V-Dipole Tracking Station (SPEC.md 15.2)
 * ESP32 DOIT DevKit V1, same wiring as the station. Serial Monitor at 115200 baud, line ending "Newline".
 *
 * It measures, then prints #define lines to paste into ../station_firmware/station_firmware.ino:
 *   i   idle noise: 30 s with hands off the pad
 *   t   touch test: three 10 s holds, when it says so
 *   r   press and release levels from i and t      -> CAP_PRESS_DELTA_US, CAP_RELEASE_DELTA_US
 *   v <Vs> <Vref>   meter readings: the CAP_DRIVE high level and CAP_REF, in volts (optional)
 *   c <pF>          one known capacitor point (optional; see step 5 below)
 *   f   fit the capacitor points                    -> CAP_VS, CAP_VREF, CAP_TD_OFFSET_US, CAP_PF_SLOPE_CAL, CAP_PF_OFFSET_CAL
 *   l   live view on or off;  x  clear the capacitor points;  h  this help
 *
 * Steps:
 *   1. Assemble the station as it will be used (lid on, final wires), power it as usual, open the Serial Monitor.
 *   2. i, keep hands and objects away from the pad for 30 s.
 *   3. t, and touch and hold the pad each time it says "touch now". Touch it the lightest way a user would.
 *   4. r. Paste the two lines it prints. If it warns the gap is small, the pad or CAP_REF needs a look first.
 *   5. Optional, for capPF in pF: measure CAP_DRIVE high and CAP_REF with a meter and send v (for example
 *      "v 3.28 2.47"). Then disconnect the pad wire and clip known capacitors, one at a time, where it connected:
 *      send "c 0" with nothing clipped, then for example "c 10", "c 22", "c 47", "c 100". Then f, and paste its lines
 *      together. capPF then reads the capacitance connected at the pad terminal.
 *
 * The measurement code is copied from the station sketch (an Arduino sketch cannot include another sketch's file).
 * `npm test` checks the copies match, so keep them the same if either changes.
 */

#include <Arduino.h>
#include "soc/gpio_reg.h"
#include "soc/soc.h"

// ---------- Copied from station_firmware.ino (kept the same; checked by tests/unit/calibration.test.mjs) ----------
#define BLOCK_T_PIN    32
#define BLOCK_R_PIN    12
#define CAP_DRIVE_PIN  4
#define CAP_COMP_PIN   16
#define CAP_R_OHMS          970000.0
#define CAP_PIN_R_OHMS      50.0
#define CAP_T_COMP_US       1.3
#define CAP_HALF_PERIOD_US  2000
#define CAP_TIMEOUT_US      2000
#define CAP_CYCLES          9

// Starting values for the two meter readings (the station sketch's CAP_VS and CAP_VREF today). `v` replaces them.
#define DEFAULT_CAP_VS      3.3
#define DEFAULT_CAP_VREF    2.475

const uint32_t CAP_DRIVE_MASK = (1UL << CAP_DRIVE_PIN);
const uint32_t CAP_COMP_MASK  = (1UL << CAP_COMP_PIN);   // GPIO0 to GPIO31 live in GPIO_IN_REG
portMUX_TYPE capMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t capCpuMHz = 240;

// One measurement. Returns the delay in microseconds, -1 if the comparator was
// already high before the drive edge, -2 on timeout.
float capMeasureOnceUs() {
  REG_WRITE(GPIO_OUT_W1TC_REG, CAP_DRIVE_MASK);
  delayMicroseconds(CAP_HALF_PERIOD_US);                    // discharge
  if (REG_READ(GPIO_IN_REG) & CAP_COMP_MASK) return -1.0f; // stuck high, sensor or comparator problem

  const uint32_t timeoutCycles = (uint32_t)CAP_TIMEOUT_US * capCpuMHz;
  uint32_t t0, t1 = 0;
  bool hit = false;

  // Poll with interrupts off on this core. The upload task runs on the other core,
  // so WiFi and TLS work cannot disturb this timing.
  portENTER_CRITICAL(&capMux);
  t0 = ESP.getCycleCount();
  REG_WRITE(GPIO_OUT_W1TS_REG, CAP_DRIVE_MASK);
  while ((ESP.getCycleCount() - t0) < timeoutCycles) {
    if (REG_READ(GPIO_IN_REG) & CAP_COMP_MASK) {
      t1 = ESP.getCycleCount();
      hit = true;
      break;
    }
  }
  portEXIT_CRITICAL(&capMux);
  REG_WRITE(GPIO_OUT_W1TC_REG, CAP_DRIVE_MASK);

  if (!hit) return -2.0f;
  return (float)(t1 - t0) / (float)capCpuMHz;
}

// Median of CAP_CYCLES good measurements. Returns a negative value on failure.
float capMedianUs() {
  float v[CAP_CYCLES];
  int n = 0;
  float lastErr = -1.0f;
  for (int i = 0; i < CAP_CYCLES; i++) {
    float x = capMeasureOnceUs();
    if (x > 0) v[n++] = x; else lastErr = x;
  }
  if (n < (CAP_CYCLES / 2 + 1)) return lastErr;
  for (int i = 1; i < n; i++) {                              // insertion sort
    float key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  return v[n / 2];
}

// ---------- Calibration ----------
#define SAMPLE_MS   100        // one median every 100 ms, like the station
#define MAX_SAMPLES 400

float samples[MAX_SAMPLES];
float idleMean = NAN, idleSd = NAN, idleMaxDev = NAN;
float holdLow[3] = {NAN, NAN, NAN}, holdMid[3] = {NAN, NAN, NAN};   // 10th percentile and median change per hold
int holdsDone = 0;
float capVs = DEFAULT_CAP_VS, capVref = DEFAULT_CAP_VREF;
struct CapPoint { float pF, tdUs; };
CapPoint points[10];
int pointCount = 0;
bool live = false;
String line;

void sortFloats(float* v, int n) {
  for (int i = 1; i < n; i++) {
    float key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
}

// Medians for `seconds`, the first `skipS` thrown away. Returns how many were good.
int collect(float seconds, float skipS) {
  int n = 0, bad = 0;
  unsigned long start = millis(), next = start;
  while (millis() - start < (unsigned long)(seconds * 1000)) {
    if (millis() < next) continue;
    next += SAMPLE_MS;
    float m = capMedianUs();
    if (millis() - start < (unsigned long)(skipS * 1000)) continue;
    if (m > 0 && n < MAX_SAMPLES) samples[n++] = m; else if (m <= 0) bad++;
  }
  if (bad) { Serial.print(F("  ")); Serial.print(bad); Serial.println(F(" failed measurements (-1 comparator stuck high, -2 timeout): check the wiring and CAP_REF.")); }
  return n;
}

void idleTest() {
  Serial.println(F("Idle: hands and objects away from the pad for 30 s..."));
  int n = collect(30, 1);
  if (n < 50) { Serial.println(F("Too few good measurements. Fix the sensor first (see above).")); return; }
  double sum = 0, sq = 0;
  for (int i = 0; i < n; i++) sum += samples[i];
  idleMean = sum / n;
  idleMaxDev = 0;
  for (int i = 0; i < n; i++) {
    float d = samples[i] - idleMean;
    sq += d * d;
    if (fabsf(d) > idleMaxDev) idleMaxDev = fabsf(d);
  }
  idleSd = sqrt(sq / (n - 1));
  Serial.print(F("  ")); Serial.print(n); Serial.print(F(" medians: mean ")); Serial.print(idleMean, 2);
  Serial.print(F(" us, spread (sd) ")); Serial.print(idleSd, 2); Serial.print(F(" us, largest change "));
  Serial.print(idleMaxDev, 2); Serial.println(F(" us"));
}

void touchTest() {
  if (isnan(idleMean)) { Serial.println(F("Run i first (idle), so the change can be measured.")); return; }
  holdsDone = 0;
  for (int h = 0; h < 3; h++) {
    Serial.print(F("Hold ")); Serial.print(h + 1); Serial.println(F(" of 3: get ready..."));
    for (int s = 3; s > 0; s--) { Serial.print(F("  ")); Serial.println(s); delay(1000); }
    Serial.println(F("  TOUCH NOW and hold for 10 s"));
    int n = collect(10, 1);
    Serial.println(F("  release"));
    if (n < 20) { Serial.println(F("  too few good measurements in this hold")); continue; }
    for (int i = 0; i < n; i++) samples[i] -= idleMean;
    sortFloats(samples, n);
    holdLow[h] = samples[n / 10];
    holdMid[h] = samples[n / 2];
    holdsDone++;
    Serial.print(F("  change: median ")); Serial.print(holdMid[h], 2); Serial.print(F(" us, weakest 10% ")); Serial.print(holdLow[h], 2);
    Serial.println(F(" us"));
    delay(3000);
  }
}

void printLevels() {
  if (isnan(idleMean) || holdsDone == 0) { Serial.println(F("Run i and then t first.")); return; }
  float weakest = NAN;
  for (int h = 0; h < 3; h++) if (!isnan(holdLow[h]) && (isnan(weakest) || holdLow[h] < weakest)) weakest = holdLow[h];
  float gap = weakest - idleMaxDev;
  Serial.print(F("Idle changes stay within ")); Serial.print(idleMaxDev, 2); Serial.print(F(" us; the weakest touch gives "));
  Serial.print(weakest, 2); Serial.println(F(" us."));
  if (!(gap > 0)) {
    Serial.println(F("The touch and idle ranges overlap: no setting would be reliable. Check the pad wire, the 1 MOhm resistor"));
    Serial.println(F("and CAP_REF, then run i and t again."));
    return;
  }
  float press = idleMaxDev + 0.6f * gap, release = idleMaxDev + 0.3f * gap;
  if (gap < 6.0f * idleSd) Serial.println(F("WARNING: the gap is small compared with the idle spread. It may miss light touches or chatter."));
  Serial.println(F("Paste into station_firmware.ino (then press b on the station to take a new baseline):"));
  Serial.print(F("#define CAP_PRESS_DELTA_US    ")); Serial.print(press, 1);
  Serial.println(F("    // from cal_capacitive: 60% of the way from idle noise to the weakest touch"));
  Serial.print(F("#define CAP_RELEASE_DELTA_US  ")); Serial.print(release, 1);
  Serial.println(F("    // from cal_capacitive: 30% of the way, lower than the press level so it does not chatter"));
}

void addPoint(float pF) {
  if (pointCount >= 10) { Serial.println(F("10 points already; x clears them.")); return; }
  Serial.print(F("Measuring with ")); Serial.print(pF, 1); Serial.println(F(" pF clipped on (5 s, hands off)..."));
  int n = collect(5, 1);
  if (n < 10) { Serial.println(F("Too few good measurements; nothing saved.")); return; }
  sortFloats(samples, n);
  points[pointCount++] = {pF, samples[n / 2]};
  Serial.print(F("  point ")); Serial.print(pointCount); Serial.print(F(": ")); Serial.print(pF, 1); Serial.print(F(" pF -> "));
  Serial.print(samples[n / 2], 2); Serial.println(F(" us"));
}

void fitPoints() {
  if (pointCount < 2) { Serial.println(F("Need at least 2 capacitor points (3 or more is better).")); return; }
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < pointCount; i++) {
    sx += points[i].pF; sy += points[i].tdUs; sxx += points[i].pF * points[i].pF; sxy += points[i].pF * points[i].tdUs;
  }
  double den = pointCount * sxx - sx * sx;
  if (den <= 0) { Serial.println(F("All points have the same capacitance; use different values.")); return; }
  double b = (pointCount * sxy - sx * sy) / den;      // us per pF
  double a = (sy - b * sx) / pointCount;              // us with nothing clipped on
  if (b <= 0) { Serial.println(F("The delay does not rise with capacitance. Check the clip-on points.")); return; }
  // Theory, as in the station: td = R x C x ln(Vs / (Vs - Vref)) + t_comp. 1 pF x 1 ohm = 1e-6 us.
  double bTheory = (CAP_R_OHMS + CAP_PIN_R_OHMS) * log(capVs / (capVs - capVref)) * 1e-6;
  double slopeCal = bTheory / b;
  double offsetCal = (CAP_T_COMP_US - a) / b;
  double worst = 0;
  Serial.println(F("  pF clipped | delay us | pF from the fit | error"));
  for (int i = 0; i < pointCount; i++) {
    double fit = (points[i].tdUs - a) / b, err = fit - points[i].pF;
    if (fabs(err) > fabs(worst)) worst = err;
    Serial.print(F("  ")); Serial.print(points[i].pF, 1); Serial.print(F(" | ")); Serial.print(points[i].tdUs, 2);
    Serial.print(F(" | ")); Serial.print(fit, 1); Serial.print(F(" | ")); Serial.println(err, 1);
  }
  Serial.print(F("Measured slope ")); Serial.print(b * 1000.0, 3); Serial.print(F(" ns per pF; theory ")); Serial.print(bTheory * 1000.0, 3);
  Serial.print(F(" (ratio ")); Serial.print(b / bTheory, 3); Serial.print(F("). Worst error ")); Serial.print(fabs(worst), 1);
  Serial.println(F(" pF."));
  if (pointCount == 2) Serial.println(F("Only 2 points: the line fits them exactly, so there is no check. A third point verifies it."));
  Serial.println(F("Paste these together into station_firmware.ino (they depend on each other):"));
  Serial.print(F("#define CAP_VS              ")); Serial.println(capVs, 3);
  Serial.print(F("#define CAP_VREF            ")); Serial.println(capVref, 3);
  Serial.println(F("#define CAP_TD_OFFSET_US    0.0"));
  Serial.print(F("#define CAP_PF_SLOPE_CAL    ")); Serial.println(slopeCal, 4);
  Serial.print(F("#define CAP_PF_OFFSET_CAL   ")); Serial.println(offsetCal, 2);
}

void help() {
  Serial.println(F("Capacitive pad calibration. i = idle 30 s, t = touch test, r = press/release levels,"));
  Serial.println(F("v <Vs> <Vref> = meter readings, c <pF> = known capacitor point, f = fit pF, l = live, x = clear points, h = help"));
}

void handle(const String& cmd) {
  if (!cmd.length()) return;
  char c = cmd[0];
  const char* rest = cmd.c_str() + 1;
  if (c == 'i') idleTest();
  else if (c == 't') touchTest();
  else if (c == 'r') printLevels();
  else if (c == 'f') fitPoints();
  else if (c == 'x') { pointCount = 0; Serial.println(F("Capacitor points cleared.")); }
  else if (c == 'l') { live = !live; Serial.println(live ? F("Live view ON") : F("Live view OFF")); }
  else if (c == 'h') help();
  else if (c == 'c') {
    char* end = nullptr;
    float pF = strtof(rest, &end);
    if (end == rest || pF < 0) Serial.println(F("Type the value in pF, for example: c 47"));
    else addPoint(pF);
  } else if (c == 'v') {
    char* end1 = nullptr;
    char* end2 = nullptr;
    float vs = strtof(rest, &end1);
    float vr = strtof(end1, &end2);
    if (end1 == rest || end2 == end1 || !(vs > 2.5f && vs < 3.6f) || !(vr > 0.3f && vr < vs - 0.05f)) {
      Serial.println(F("Type both meter readings in volts, for example: v 3.28 2.47 (Vs between 2.5 and 3.6, Vref below Vs)"));
    } else {
      capVs = vs; capVref = vr;
      Serial.print(F("Using CAP_VS ")); Serial.print(capVs, 3); Serial.print(F(" V and CAP_VREF ")); Serial.print(capVref, 3);
      Serial.println(F(" V."));
    }
  } else {
    help();
  }
}

void setup() {
  // Same idle state as the station: both mux channels blocked, drive low.
  pinMode(BLOCK_T_PIN, OUTPUT);
  pinMode(BLOCK_R_PIN, OUTPUT);
  digitalWrite(BLOCK_T_PIN, HIGH);
  digitalWrite(BLOCK_R_PIN, HIGH);
  pinMode(CAP_DRIVE_PIN, OUTPUT);
  REG_WRITE(GPIO_OUT_W1TC_REG, CAP_DRIVE_MASK);
  pinMode(CAP_COMP_PIN, INPUT);
  capCpuMHz = getCpuFrequencyMhz();
  Serial.begin(115200);
  delay(500);
  help();
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') { line.trim(); handle(line); line = ""; }
    else if (line.length() < 40) line += ch;
  }
  static unsigned long lastLive = 0;
  if (live && millis() - lastLive >= 500) {
    lastLive = millis();
    float m = capMedianUs();
    Serial.print(F("td ")); Serial.print(m, 2); Serial.print(F(" us"));
    if (!isnan(idleMean) && m > 0) { Serial.print(F("  change ")); Serial.print(m - idleMean, 2); Serial.print(F(" us")); }
    Serial.println();
  }
}
