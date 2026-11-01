/*
 * Thermistor calibration for the Satellite V-Dipole Tracking Station (SPEC.md 15.2)
 * ESP32 DOIT DevKit V1, same wiring as the station. Serial Monitor at 115200 baud, line ending "Newline".
 *
 * It works out the thermistor's resistance from the circuit at a few known temperatures, fits the beta model
 *   ln R = ln R0 + B x (1/T - 1/T0),  T in kelvin, T0 = 25 C
 * and prints THERM_R0, THERM_BETA and THERM_OFFSET_C to paste into ../station_firmware/station_firmware.ino.
 *
 *   l               live view on or off (resistance, the fitted temperature once there is a fit, the BMP388, "steady")
 *   p <reference C> capture a point at a known temperature, for example "p 0.0" in ice water
 *   p               capture a point using the BMP388 as the reference (only when both sit in the same still air)
 *   d  drop the last point;  x  clear all points;  f  fit and print;  h  help
 *
 * Steps:
 *   1. Use a reference thermometer you trust (or the BMP388 for the room point).
 *   2. At least 2 points 10 C or more apart; 3 is better (the third checks the fit). For example: ice water
 *      (0 C, stirred, thermistor in a thin plastic bag), room air, warm water (about 40 C, read the thermometer).
 *   3. At each one: wait until the live view says "steady" (l), then p with the reference reading.
 *   4. f, then paste the three lines it prints.
 * If the resistance comes out impossible, the circuit constants below are wrong rather than the thermistor: measure
 * BRIDGE_VEXC, V_MID and V_BIAS with a meter and correct them here AND in the station sketch.
 *
 * The circuit constants and measurement code are copied from the station sketch (an Arduino sketch cannot include
 * another sketch's file). `npm test` checks the copies match, so keep them the same if either changes.
 */

#include <Wire.h>
#include <Adafruit_BMP3XX.h>

// ---------- Copied from station_firmware.ino (kept the same; checked by tests/unit/calibration.test.mjs) ----------
#define SDA_PIN        21
#define SCL_PIN        22
#define MUX_ADC_PIN    34
#define BLOCK_T_PIN    32
#define BLOCK_R_PIN    12
#define BMP388_I2C_ADDR       0x77

#define ADC_MAX_COUNT 4095.0
#define ADC_VREF      3.3

#define MUX_R_IN_OHMS      10000.0
#define MUX_R_MIX_OHMS     10000.0
#define MUX_RDS_ON_OHMS    100.0
#define MUX_GAIN_THEORY \
  ((MUX_R_MIX_OHMS + MUX_RDS_ON_OHMS) / (MUX_R_IN_OHMS + 2.0 * MUX_R_MIX_OHMS + MUX_RDS_ON_OHMS))
#define MUX_GAIN_T_MEASURED  0.0
#define MUX_SETTLE_MS   20
#define MUX_SAMPLES     16

#define THERM_T0_C      25.0
#define R_P             207.0
#define BRIDGE_VEXC     4.84
#define BRIDGE_R6       198.0
#define BRIDGE_R5       181.0
#define BRIDGE_R7       181.0
#define INA121_RG       1000.0
#define V_MID           2.485
#define V_BIAS          1.64
#define R12             10000.0
#define R13             10000.0
#define R14             10000.0
#define R15             10000.0

const float MUX_GAIN_T = (MUX_GAIN_T_MEASURED > 0.0) ? MUX_GAIN_T_MEASURED : MUX_GAIN_THEORY;

enum MuxChannel { MUX_NONE, MUX_THERM, MUX_RF };

// HIGH on a BLOCK pin blocks that channel, so the selected one gets LOW.
void muxSelect(MuxChannel ch) {
  digitalWrite(BLOCK_T_PIN, ch == MUX_THERM ? LOW : HIGH);
  digitalWrite(BLOCK_R_PIN, ch == MUX_RF ? LOW : HIGH);
}

int muxReadCounts(MuxChannel ch) {
  muxSelect(ch);
  delay(MUX_SETTLE_MS);
  analogRead(MUX_ADC_PIN);            // throw away one sample, the ADC sample cap still holds the old channel
  long sum = 0;
  for (int i = 0; i < MUX_SAMPLES; i++) sum += analogRead(MUX_ADC_PIN);
  muxSelect(MUX_NONE);                // idle with both channels blocked
  return (int)(sum / MUX_SAMPLES);
}

float countsToPinVolts(int counts) {
  return (counts / ADC_MAX_COUNT) * ADC_VREF;
}

// The first half of the station's thermistorVoltsToCelsius(): the thermistor's resistance from V_out (the voltage at
// the difference-amp output, mux gain already undone). NaN or a silly number means a circuit constant is wrong.
float thermistorVoltsToOhms(float V_out) {
  // Invert the difference amp
  float V_plus = ((V_MID / R13) + (V_BIAS / R15)) / ((1.0 / R13) + (1.0 / R15));
  float k = R14 / R12;
  float V_ina_out = (V_plus * (1.0 + k) - V_out) / k;

  // Invert the INA121
  float ina_gain = 1.0 + (50000.0 / INA121_RG);
  float V_bridge = (V_ina_out - V_MID) / ina_gain;

  // Invert the Wheatstone bridge: +5V -> (TH||Rp) -> NodeA -> R6 -> GND, +5V -> R5 -> NodeB -> R7 -> GND
  float NodeB = BRIDGE_VEXC * BRIDGE_R7 / (BRIDGE_R5 + BRIDGE_R7);
  float NodeA = NodeB + V_bridge;
  float R_par = (BRIDGE_VEXC * BRIDGE_R6 / NodeA) - BRIDGE_R6;

  // Invert the parallel-resistor linearization
  float R_T = (R_par * R_P) / (R_P - R_par);
  return R_T;
}

// ---------- Calibration ----------
#define POINT_READS    30           // a point is the mean of this many readings, 100 ms apart
#define STEADY_N       30           // "steady" when the last 30 live readings agree within STEADY_FRAC
#define STEADY_FRAC    0.0025

Adafruit_BMP3XX bmp;
bool bmpOk = false;
struct Point { float refC, ohms, vOut; };
Point points[8];
int pointCount = 0;
bool haveFit = false;
double fitR0 = NAN, fitBeta = NAN, fitOffset = 0;
bool live = false;
float recent[STEADY_N];
int recentN = 0, recentHead = 0;
String line;

float readVOut() { return countsToPinVolts(muxReadCounts(MUX_THERM)) / MUX_GAIN_T; }

bool ohmsPossible(float r) { return isfinite(r) && r >= 10.0f && r <= 1.0e6f; }

float fittedC(float ohms) {
  double invT = 1.0 / (THERM_T0_C + 273.15) + log(ohms / fitR0) / fitBeta;
  return (float)(1.0 / invT - 273.15 + fitOffset);
}

float bmpC() {
  if (!bmpOk || !bmp.performReading()) return NAN;
  return bmp.temperature;
}

void explainImpossible(float r) {
  Serial.print(F("  The resistance worked out from the circuit is impossible: ")); Serial.print(r, 1); Serial.println(F(" ohm."));
  Serial.println(F("  That means a circuit constant is wrong, not the thermistor. Measure with a meter and correct, here"));
  Serial.println(F("  and in station_firmware.ino: BRIDGE_VEXC (bridge supply), V_MID (MC1403U output), V_BIAS. Also run m"));
  Serial.println(F("  in the station sketch to check the mux gain (MUX_GAIN_T_MEASURED)."));
}

void capturePoint(float refC) {
  if (pointCount >= 8) { Serial.println(F("8 points already; x clears them.")); return; }
  Serial.println(F("Capturing (3 s)..."));
  double sumR = 0, sumV = 0, minR = 1e30, maxR = -1e30;
  for (int i = 0; i < POINT_READS; i++) {
    float v = readVOut(), r = thermistorVoltsToOhms(v);
    sumV += v; sumR += r;
    if (r < minR) minR = r;
    if (r > maxR) maxR = r;
    delay(60);
  }
  float r = sumR / POINT_READS, v = sumV / POINT_READS;
  if (!ohmsPossible(r)) { explainImpossible(r); return; }
  points[pointCount++] = {refC, r, v};
  haveFit = false;
  Serial.print(F("  point ")); Serial.print(pointCount); Serial.print(F(": ")); Serial.print(refC, 2); Serial.print(F(" C, "));
  Serial.print(r, 2); Serial.print(F(" ohm (spread ")); Serial.print((maxR - minR) / r * 100.0, 2); Serial.print(F("%), V_out "));
  Serial.print(v, 4); Serial.println(F(" V"));
}

void fit() {
  if (pointCount < 2) { Serial.println(F("Need at least 2 points (3 or more is better).")); return; }
  float lo = points[0].refC, hi = points[0].refC;
  for (int i = 0; i < pointCount; i++) { lo = min(lo, points[i].refC); hi = max(hi, points[i].refC); }
  if (hi - lo < 10.0f) { Serial.println(F("The points must span at least 10 C (for example ice water and room air).")); return; }
  // Least squares of y = ln R against x = 1/T: slope B, intercept c0 = ln R0 - B / T0.
  double sx = 0, sy = 0, sxx = 0, sxy = 0, t0 = THERM_T0_C + 273.15;
  for (int i = 0; i < pointCount; i++) {
    double x = 1.0 / (points[i].refC + 273.15), y = log(points[i].ohms);
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  double n = pointCount, beta = (n * sxy - sx * sy) / (n * sxx - sx * sx), c0 = (sy - beta * sx) / n;
  if (!(beta > 0)) {
    Serial.println(F("The resistance rises with temperature, which a thermistor of this kind (NTC) does not do."));
    Serial.println(F("Check the reference readings, then the circuit constants (see the top of this sketch)."));
    return;
  }
  fitBeta = beta;
  fitR0 = exp(c0 + beta / t0);
  fitOffset = 0;
  double meanErr = 0;
  for (int i = 0; i < pointCount; i++) meanErr += (fittedC(points[i].ohms) - points[i].refC) / n;
  fitOffset = -meanErr;
  haveFit = true;
  Serial.println(F("  reference C | ohm | fitted C | error C"));
  for (int i = 0; i < pointCount; i++) {
    float t = fittedC(points[i].ohms);
    Serial.print(F("  ")); Serial.print(points[i].refC, 2); Serial.print(F(" | ")); Serial.print(points[i].ohms, 2);
    Serial.print(F(" | ")); Serial.print(t, 2); Serial.print(F(" | ")); Serial.println(t - points[i].refC, 2);
  }
  if (pointCount == 2) Serial.println(F("Only 2 points: the curve fits them exactly, so there is no check. A third point verifies it."));
  Serial.println(F("Paste into station_firmware.ino (replacing the old values; the old +96.2 offset goes away):"));
  Serial.print(F("#define THERM_R0        ")); Serial.print(fitR0, 2); Serial.println(F("    // ohm at 25 C, fitted by cal_thermistor"));
  Serial.print(F("#define THERM_BETA      ")); Serial.print(fitBeta, 1); Serial.println(F("    // K, fitted by cal_thermistor"));
  Serial.print(F("#define THERM_OFFSET_C  ")); Serial.print(fitOffset, 2); Serial.println(F("    // C left after the fit; near 0"));
}

void help() {
  Serial.println(F("Thermistor calibration. l = live, p <C> = point at a known temperature, p = point using the BMP388,"));
  Serial.println(F("d = drop last point, x = clear points, f = fit and print, h = help"));
}

void handle(const String& cmd) {
  if (!cmd.length()) return;
  char c = cmd[0];
  const char* rest = cmd.c_str() + 1;
  if (c == 'l') { live = !live; recentN = 0; Serial.println(live ? F("Live view ON") : F("Live view OFF")); }
  else if (c == 'f') fit();
  else if (c == 'x') { pointCount = 0; haveFit = false; Serial.println(F("Points cleared.")); }
  else if (c == 'd') { if (pointCount) pointCount--; haveFit = false; Serial.print(pointCount); Serial.println(F(" points left.")); }
  else if (c == 'h') help();
  else if (c == 'p') {
    char* end = nullptr;
    float ref = strtof(rest, &end);
    if (end == rest) {
      ref = bmpC();
      if (isnan(ref)) { Serial.println(F("No BMP388 reading. Type the reference temperature instead, for example: p 21.4")); return; }
      Serial.print(F("Using the BMP388: ")); Serial.print(ref, 2);
      Serial.println(F(" C. Only fair if the thermistor and the BMP388 sit in the same still air and have settled."));
    }
    if (ref < -30 || ref > 100) { Serial.println(F("That reference is out of range (-30 to 100 C).")); return; }
    capturePoint(ref);
  } else {
    help();
  }
}

void setup() {
  // Block both mux channels before anything else, like the station.
  pinMode(BLOCK_T_PIN, OUTPUT);
  pinMode(BLOCK_R_PIN, OUTPUT);
  muxSelect(MUX_NONE);
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  bmpOk = bmp.begin_I2C(BMP388_I2C_ADDR);
  if (bmpOk) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp.setOutputDataRate(BMP3_ODR_50_HZ);
  }
  delay(500);
  Serial.println(bmpOk ? F("BMP388 OK (usable for the room point)") : F("BMP388 not found: type every reference temperature."));
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
  if (live && millis() - lastLive >= 1000) {
    lastLive = millis();
    float v = readVOut(), r = thermistorVoltsToOhms(v);
    recent[recentHead] = r;
    recentHead = (recentHead + 1) % STEADY_N;
    if (recentN < STEADY_N) recentN++;
    float lo = r, hi = r;
    for (int i = 0; i < recentN; i++) { lo = min(lo, recent[i]); hi = max(hi, recent[i]); }
    bool steady = recentN == STEADY_N && ohmsPossible(r) && (hi - lo) <= STEADY_FRAC * r;
    Serial.print(F("V_out ")); Serial.print(v, 4); Serial.print(F(" V  R ")); Serial.print(r, 2); Serial.print(F(" ohm"));
    if (haveFit && ohmsPossible(r)) { Serial.print(F("  fitted ")); Serial.print(fittedC(r), 2); Serial.print(F(" C")); }
    Serial.print(F("  BMP388 ")); Serial.print(bmpC(), 2); Serial.print(F(" C  "));
    Serial.println(!ohmsPossible(r) ? F("IMPOSSIBLE RESISTANCE (see the top of this sketch)") : steady ? F("steady") : F("settling"));
  }
}
