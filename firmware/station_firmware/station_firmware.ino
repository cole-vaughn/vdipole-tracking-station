/*
 * Satellite V-Dipole Tracking Station: complete ESP32 firmware (version 1.2)
 * ELEE 4230, ESP32 DOIT DevKit V1
 *
 * Everything in one sketch (plus rqs.h):
 *   - BMP388 temperature and pressure (I2C 0x77)
 *   - Thermistor chain and RF (AD8307) chain through the shared-ADC analog mux,
 *     each with a Kalman filter
 *   - Capacitive press sensor (square wave + LM339 timing), with press counter
 *   - INA219 on the 12 V line: voltage, current, power (128-sample averaging, read every 100 ms)
 *   - Battery estimator (energy counting + voltage check, saved to flash)
 *   - Reception quality score from the RF reading (rqs.h, SPEC.md section 14), with NTP time and a pass schedule
 *   - PC fan speed from the tach wire (3-pin fan)
 *   - 16x2 LCD with 5 pages (a capacitive press changes the page; it starts on RF and the score)
 *   - Firebase Realtime Database upload (replaces ThingSpeak), in its own task
 *
 * Libraries (Library Manager): Adafruit BMP3XX, Adafruit INA219, Adafruit BusIO,
 * Adafruit Unified Sensor. Built in: Wire, LiquidCrystal, WiFi, HTTPClient, Preferences.
 *
 * Setup: copy secrets.example.h to secrets.h and fill it in. See ../README.md.
 *
 * Search this file and rqs.h for "PLACEHOLDER" to find every value that still needs a bench measurement.
 *
 * NOTE: version 1.2 compiles with arduino-cli (esp32 core 3.3.12) but has not run on the station yet.
 * Test it with the checklist in ../README.md. The score logic in rqs.h is tested on a PC (npm run test:firmware).
 */

#include <Wire.h>
#include <Adafruit_BMP3XX.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "secrets.h"
#include "rqs.h"

#ifndef PASSES_URL
#define PASSES_URL ""                 // an older secrets.h without it: no pass schedule (tag NO SCHEDULE)
#endif

#define FW_VERSION "vdipole-fw-1.2"

// Shared telemetry snapshot (loop() writes, the upload task reads). Declared up here because the Arduino IDE
// generates function prototypes at the top of the sketch, and buildTelemetryJson() takes this type.
struct Telemetry {
  float tempC, pressHPa, altitudeM;
  float thermRaw, thermFilt, rfRawDBm, rfFilt;
  float capTdUs, capDeltaUs, capPF;
  bool  capPressed, capFault;
  uint32_t touchCount;
  bool  inaOk;
  float battV, battmA, battmW, battSocPct, battWhLeft, battHoursLeft, battPeakmA;
  float fanRpm;
  bool  fanStalled;
  int   rssi;
  uint32_t uptimeS;
  rqs::Result rq;
};

// ---------- General config ----------
#define SEALEVELPRESSURE_HPA  (1013.25)
#define READ_INTERVAL_MS      2000    // BMP388, mux sensors, INA219, fan, battery
#define CAP_INTERVAL_MS       100     // capacitive sensor, fast so a press feels instant
#define INA_SAMPLE_MS         100     // INA219 read this often; each READ_INTERVAL_MS window is averaged
#define LCD_REFRESH_MS        250
#define SCREEN_COUNT          5
#define BMP388_I2C_ADDR       0x77
#define INA219_I2C_ADDR       0x40    // A0 and A1 tied to GND

// ---------- Firebase config ----------
#define LATEST_INTERVAL_MS    2000    // overwrites /station/latest (no storage growth)
#define HISTORY_INTERVAL_MS   10000   // appends to /station/history (about 640 bytes per record)
#define TOKEN_REFRESH_MS      (50UL * 60UL * 1000UL)   // sign in again before the 1 hour token expires
#define TOKEN_EXPIRY_MS       (58UL * 60UL * 1000UL)   // after this long the old token is treated as dead
#define SIGNIN_MIN_GAP_MS     15000UL                  // never sign in more often than this

// ---------- Pins ----------
#define SDA_PIN        21
#define SCL_PIN        22
#define MUX_ADC_PIN    34   // MUX_OUT. ADC1, input only. Both raw sensors land here.
#define BLOCK_T_PIN    32   // HIGH turns the thermistor shunt transistor on, blocking that channel
#define BLOCK_R_PIN    12   // HIGH turns the RF shunt transistor on, blocking that channel. Strapping pin: nothing may pull it HIGH at boot.
#define CAP_DRIVE_PIN  4    // square wave out, through R16 to the sensor
#define CAP_COMP_PIN   16   // LM339 output in, 10k pull-up to 3.3 V on the board.
#define TACH_PIN       18   // fan tach wire, through a 10k series resistor. 10k pull-up to 3.3 V on the fan side, see README.
// LCD (4-bit): RS 27, E 26, D4 25, D5 33, D6 14, D7 13

#define ADC_MAX_COUNT 4095.0
#define ADC_VREF      3.3

// ---------- Fan ----------
#define FAN_PULSES_PER_REV   2        // standard 3-pin and 4-pin PC fans give 2 tach pulses per turn
#define TACH_DEBOUNCE_US     2000     // ignore edges closer than this (a 3000 RPM fan has 10000 us between pulses)
#define FAN_STALL_RPM        200      // below this, with the system running, report a stall
                                      // (measured: about 1050 to 1110 RPM on the 12 V battery, about 650 on USB power)
#define FAN_STALL_GRACE_MS   10000    // do not report a stall during the first seconds after boot
#define FAN_ENABLED          1        // set to 0 until the tach wire is connected, so no false "stalled" is reported

// ===================================================================
// ANALOG MUX
// ===================================================================
// Each channel: sensor -> R_in -> node -> R_mix -> MUX_OUT. A shunt transistor
// from each node to ground blocks that channel when it is on. The selected
// channel therefore sees R_mix into the blocked channel's R_mix and shunt:
//   gain = (R_mix + Rds_on) / (R_in + 2*R_mix + Rds_on)
#define MUX_R_IN_OHMS      10000.0  // R9, R12
#define MUX_R_MIX_OHMS     10000.0  // R13, R15
#define MUX_RDS_ON_OHMS    100.0    // PLACEHOLDER: BC547 shunt, about 100 ohm. Replace with the measured value.
#define MUX_GAIN_THEORY \
  ((MUX_R_MIX_OHMS + MUX_RDS_ON_OHMS) / (MUX_R_IN_OHMS + 2.0 * MUX_R_MIX_OHMS + MUX_RDS_ON_OHMS))

// Measured gain per channel: V at MUX_OUT divided by V at the sensor output. 0.0 means "use theory".
#define MUX_GAIN_T_MEASURED  0.0    // PLACEHOLDER
#define MUX_GAIN_R_MEASURED  0.0    // PLACEHOLDER

#define MUX_SETTLE_MS   20
#define MUX_SAMPLES     16          // averaged per channel

const float MUX_GAIN_T = (MUX_GAIN_T_MEASURED > 0.0) ? MUX_GAIN_T_MEASURED : MUX_GAIN_THEORY;
const float MUX_GAIN_R = (MUX_GAIN_R_MEASURED > 0.0) ? MUX_GAIN_R_MEASURED : MUX_GAIN_THEORY;

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

// ===================================================================
// THERMISTOR CHAIN (unchanged from CP4)
// ===================================================================
#define THERM_R0        1025.3     // measured R at 25 C
#define THERM_BETA      902.0      // measured beta, K
#define THERM_T0_C      25.0

#define R_P             207.0      // linearizing resistor, R17 in the schematic

#define BRIDGE_VEXC     4.84       // measured rail at the bridge
#define BRIDGE_R6       198.0
#define BRIDGE_R5       181.0
#define BRIDGE_R7       181.0

#define INA121_RG       1000.0     // gain 51

#define V_MID           2.485      // MC1403U reference
#define V_BIAS          1.64

#define R12             10000.0
#define R13             10000.0
#define R14             10000.0
#define R15             10000.0

// PLACEHOLDER: offset added to the THERMISTOR result (never to the BMP388), from the CP3 comparison with the BMP388.
// It was called BMP388_CALIBRATION_OFFSET before 1.2. Run ../cal_thermistor and paste what it prints.
#define THERM_OFFSET_C  96.2

struct KalmanFilter1D {
  float estimate;
  float errorCovariance;
  float processNoise;      // Q
  float measurementNoise;  // R

  void init(float initialEstimate, float q, float r) {
    estimate = initialEstimate;
    errorCovariance = 1.0;
    processNoise = q;
    measurementNoise = r;
  }

  float update(float measurement) {
    errorCovariance += processNoise;
    float kalmanGain = errorCovariance / (errorCovariance + measurementNoise);
    estimate = estimate + kalmanGain * (measurement - estimate);
    errorCovariance = (1.0 - kalmanGain) * errorCovariance;
    return estimate;
  }
};

KalmanFilter1D thermKalman;
KalmanFilter1D rfKalman;

// V_out is the voltage at the difference-amp output, so the mux gain is already undone.
float thermistorVoltsToCelsius(float V_out) {
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

  // Invert the parallel-resistor linearization, then the beta equation
  float R_T = (R_par * R_P) / (R_P - R_par);
  float T0_kelvin = THERM_T0_C + 273.15;
  float invT = (log(R_T / THERM_R0) / THERM_BETA) + (1.0 / T0_kelvin);
  float T_celsius = (1.0 / invT) - 273.15;

  return T_celsius + THERM_OFFSET_C;
}

// ===================================================================
// RF CHAIN (unchanged from CP4)
// ===================================================================
#define RF_GAIN_R_IN   10000.0
#define RF_GAIN_R_F    2930.0      // measured stage gain 1.293

#define AD8307_SLOPE_V_PER_DB   0.025
#define AD8307_INTERCEPT_DBM    -84.0

// V_adc is the voltage at the LM324 gain-stage output, so the mux gain is already undone.
float rfVoltsToDBm(float V_adc) {
  float rf_gain = 1.0 + (RF_GAIN_R_F / RF_GAIN_R_IN);
  float V_ad8307 = V_adc / rf_gain;
  return (V_ad8307 / AD8307_SLOPE_V_PER_DB) + AD8307_INTERCEPT_DBM;
}

// ===================================================================
// CAPACITIVE SENSOR (unchanged from CP4)
// ===================================================================
// ESP32 drives CAP_DRIVE high through R16 into the sensor C. The node voltage
// rises as V(t) = Vs(1 - e^(-t/RC)). An LM339 compares it to CAP_REF and its
// output goes high at the crossing. The ESP32 times drive edge to comparator
// edge. A touch raises C, which raises the delay:
//   td = R*C*ln[Vs / (Vs - Vref)] + t_comp
#define CAP_R_OHMS          970000.0  // measured 0.97 MOhm
#define CAP_PIN_R_OHMS      50.0
#define CAP_VS              3.3       // PLACEHOLDER: measure the GPIO high level
#define CAP_VREF            2.475     // PLACEHOLDER: measure CAP_REF
#define CAP_T_COMP_US       1.3       // PLACEHOLDER: LM339 response time

#define CAP_HALF_PERIOD_US  2000
#define CAP_TIMEOUT_US      2000
#define CAP_CYCLES          9         // median of this many cycles per update

#define CAP_TD_OFFSET_US    0.0       // PLACEHOLDER
#define CAP_PF_SLOPE_CAL    1.0       // PLACEHOLDER
#define CAP_PF_OFFSET_CAL   0.0       // PLACEHOLDER

// With the longer foil wires and the enclosure lid, re-run 'b' (new baseline) and re-tune these.
#define CAP_PRESS_DELTA_US    27.0    // PLACEHOLDER: tune on hardware
#define CAP_RELEASE_DELTA_US  18.0    // PLACEHOLDER: lower than the press level so it does not chatter
#define CAP_CONFIRM_READS     2
#define CAP_BASELINE_ALPHA    0.01
#define CAP_BASELINE_SETS     20

const uint32_t CAP_DRIVE_MASK = (1UL << CAP_DRIVE_PIN);
const uint32_t CAP_COMP_MASK  = (1UL << CAP_COMP_PIN);   // GPIO0 to GPIO31 live in GPIO_IN_REG
portMUX_TYPE capMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t capCpuMHz = 240;

float capTdUs = NAN;          // calibrated median delay
float capDeltaUs = 0;         // delay minus baseline
float capPF = NAN;            // capacitance estimate
float capBaselineUs = NAN;
bool  capBaselineValid = false;
bool  capPressed = false;
bool  capFault = false;
int   capConfirmCount = 0;
unsigned long capPressCount = 0;
bool  capPressEvent = false;  // set on a new press, cleared by loop()
bool  capStream = false;

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

// Theoretical pF per microsecond, then the placeholder calibration on top.
float capDelayToPF(float tdUs) {
  float rTotal = CAP_R_OHMS + CAP_PIN_R_OHMS;
  float lnFactor = log(CAP_VS / (CAP_VS - CAP_VREF));
  float pfPerUs = 1.0e6 / (rTotal * lnFactor);
  return (tdUs - CAP_T_COMP_US) * pfPerUs * CAP_PF_SLOPE_CAL + CAP_PF_OFFSET_CAL;
}

bool capCaptureBaseline() {
  float sum = 0;
  int good = 0;
  for (int i = 0; i < CAP_BASELINE_SETS; i++) {
    float m = capMedianUs();
    if (m > 0) { sum += m + CAP_TD_OFFSET_US; good++; }
  }
  if (good < CAP_BASELINE_SETS / 2) { capBaselineValid = false; return false; }
  capBaselineUs = sum / good;
  capBaselineValid = true;
  capPressed = false;
  capConfirmCount = 0;
  return true;
}

void capUpdate() {
  float m = capMedianUs();
  if (m < 0) {
    capFault = true;
    return;
  }
  capFault = false;
  capTdUs = m + CAP_TD_OFFSET_US;
  capPF = capDelayToPF(capTdUs);
  if (!capBaselineValid) return;
  capDeltaUs = capTdUs - capBaselineUs;

  if (!capPressed) {
    if (capDeltaUs > CAP_PRESS_DELTA_US) {
      if (++capConfirmCount >= CAP_CONFIRM_READS) {
        capPressed = true;
        capPressEvent = true;
        capPressCount++;
        capConfirmCount = 0;
      }
    } else {
      capConfirmCount = 0;
      if (fabs(capDeltaUs) < CAP_RELEASE_DELTA_US) {         // track slow drift only while idle
        capBaselineUs += CAP_BASELINE_ALPHA * capDeltaUs;
      }
    }
  } else {
    if (capDeltaUs < CAP_RELEASE_DELTA_US) {
      if (++capConfirmCount >= CAP_CONFIRM_READS) {
        capPressed = false;
        capConfirmCount = 0;
      }
    } else {
      capConfirmCount = 0;
    }
  }
}

// ===================================================================
// INA219 (12 V line, between the button and the buck converter)
// ===================================================================
Adafruit_INA219 ina219(INA219_I2C_ADDR);
bool inaOk = false;
float inaBusV = NAN, inaShuntmV = NAN, inaCurrentmA = NAN, inaPowermW = NAN;

// Two-point calibration against a reference meter in series with the load.
#define INA219_CURRENT_GAIN       1.0   // PLACEHOLDER
#define INA219_CURRENT_OFFSET_MA  0.0   // PLACEHOLDER
#define INA219_CALIBRATED         0     // set to 1 once the two values above are measured (the website then drops "uncalibrated")

// The chip averages 128 samples per reading (69 ms), so WiFi bursts no longer look like random current noise.
// Same range and gain as setCalibration_32V_1A(); the library has no call for averaging, so this is written directly.
const uint16_t INA219_CONFIG_AVG128 =
    (uint16_t)INA219_CONFIG_BVOLTAGERANGE_32V | (uint16_t)INA219_CONFIG_GAIN_8_320MV |
    (uint16_t)INA219_CONFIG_BADCRES_12BIT_128S_69MS | (uint16_t)INA219_CONFIG_SADCRES_12BIT_128S_69MS |
    (uint16_t)INA219_CONFIG_MODE_SANDBVOLT_CONTINUOUS;
uint32_t inaConfigRewrites = 0;       // times the chip had lost the setting (a reset from a sharp load), shown by `q`

uint16_t ina219ReadConfig() {
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_CONFIG);
  if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)INA219_I2C_ADDR, (uint8_t)2) != 2) return 0;
  uint16_t hi = Wire.read();
  return (uint16_t)((hi << 8) | Wire.read());
}

void ina219WriteConfig() {
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_CONFIG);
  Wire.write((uint8_t)(INA219_CONFIG_AVG128 >> 8));
  Wire.write((uint8_t)(INA219_CONFIG_AVG128 & 0xFF));
  Wire.endTransmission();
}

// Sums for the present 2 s window, filled every INA_SAMPLE_MS.
double inaSumBusV = 0, inaSumShuntmV = 0, inaSummA = 0, inaSummW = 0;
uint16_t inaSumN = 0;

void inaSample() {
  if (!inaOk) return;
  float bus   = ina219.getBusVoltage_V();                  // measured at Vin- (load side)
  float shunt = ina219.getShuntVoltage_mV();
  float mA    = ina219.getCurrent_mA() * INA219_CURRENT_GAIN + INA219_CURRENT_OFFSET_MA;
  float battV = bus + shunt / 1000.0f;                     // battery side (Vin+)
  inaSumBusV += bus;  inaSumShuntmV += shunt;  inaSummA += mA;  inaSummW += battV * mA;
  inaSumN++;
}

// Called every READ_INTERVAL_MS: the window's averages become the readings everything else uses.
void readINA219() {
  if (!inaOk) return;
  if (inaSumN == 0) inaSample();       // the very first call, before the 100 ms reads have started
  inaBusV      = inaSumBusV / inaSumN;
  inaShuntmV   = inaSumShuntmV / inaSumN;
  inaCurrentmA = inaSummA / inaSumN;
  inaPowermW   = inaSummW / inaSumN;   // mean of V x I, not mean V x mean I
  inaSumBusV = inaSumShuntmV = inaSummA = inaSummW = 0;
  inaSumN = 0;
  if (ina219ReadConfig() != INA219_CONFIG_AVG128) {
    ina219WriteConfig();
    inaConfigRewrites++;
  }
}

// ===================================================================
// BATTERY ESTIMATOR (KBT 12V 2400 mAh, 3S Li-ion, label 26.6 Wh)
// ===================================================================
// 1. Energy counting: watt-hours drawn are subtracted from the watt-hours left.
//    The load is a buck converter (roughly constant power), so watt-hours beat amp-hours.
// 2. Voltage check: the sag-corrected battery voltage gives a second charge estimate,
//    trusted only near full and near empty, and used to nudge the count.
// 3. Time left = energy left / smoothed average power.
// 4. Energy left is saved to flash every 5 minutes.
#define BATT_CELLS              3
#define BATT_CAP_WH_RATED       26.6f
#define BATT_USABLE_FRACTION    0.90f   // PLACEHOLDER: derating for age and optimistic labels. 1.0 trusts the label.
#define BATT_R_INT_OHM          0.25f   // PLACEHOLDER: pack + wiring resistance for the sag correction
#define BATT_AVG_TAU_S          120.0f
#define BATT_CORR_PER_S         0.001f
#define BATT_SAVE_EVERY_S       300
#define BATT_LOW_PERCENT        20
#define BATT_LOW_VOLTS          9.5f    // 9.0 V is the pack cutoff

const float BATT_CAP_WH = BATT_CAP_WH_RATED * BATT_USABLE_FRACTION;

// Typical 18650 open-circuit voltage per cell -> percent
const float CELL_V[]   = {4.20, 4.15, 4.11, 4.08, 4.02, 3.98, 3.95, 3.91, 3.87, 3.85, 3.84,
                          3.82, 3.80, 3.79, 3.77, 3.75, 3.73, 3.71, 3.69, 3.61, 3.30, 3.00};
const float CELL_PCT[] = { 100,   95,   90,   85,   80,   75,   70,   65,   60,   55,   50,
                            45,   40,   35,   30,   25,   20,   15,   10,    5,    2,    0};
const int   CELL_TABLE_N = sizeof(CELL_V) / sizeof(CELL_V[0]);

Preferences prefs;
float battRemWh = 0, battAvgPmW = 0, battUsedWh = 0, battPeakmA = 0;
float battLoadV = NAN, battSocPct = NAN, battHoursLeft = NAN;
bool  battAvgInit = false;
float battLastSavedWh = -1;
unsigned long battLastSaveMs = 0, battLastUpdateMs = 0;

float socFromVoltage(float packV) {
  float v = packV / BATT_CELLS;
  if (v >= CELL_V[0]) return 100;
  if (v <= CELL_V[CELL_TABLE_N - 1]) return 0;
  for (int i = 0; i < CELL_TABLE_N - 1; i++) {
    if (v >= CELL_V[i + 1]) {
      float f = (v - CELL_V[i + 1]) / (CELL_V[i] - CELL_V[i + 1]);
      return CELL_PCT[i + 1] + f * (CELL_PCT[i] - CELL_PCT[i + 1]);
    }
  }
  return 0;
}

void battSave() {
  prefs.putFloat("remWh", battRemWh);
  battLastSavedWh = battRemWh;
  battLastSaveMs = millis();
}

void batteryInit() {
  prefs.begin("station", false);
  battLastUpdateMs = millis();
  if (!inaOk) return;
  float v   = inaBusV + inaShuntmV / 1000.0f;
  float socV = socFromVoltage(v + (inaCurrentmA / 1000.0f) * BATT_R_INT_OHM);
  float saved = prefs.getFloat("remWh", -1);
  if (socV >= 98) {
    battRemWh = BATT_CAP_WH;
    Serial.println(F("Battery: voltage says FULL, starting at 100%."));
  } else if (saved >= 0 && fabs(saved / BATT_CAP_WH * 100.0f - socV) <= 20.0f) {
    battRemWh = constrain(saved, 0.0f, BATT_CAP_WH);
    Serial.println(F("Battery: resuming from the value saved in flash."));
  } else {
    battRemWh = BATT_CAP_WH * socV / 100.0f;
    Serial.println(F("Battery: no usable saved value, starting from the voltage reading."));
  }
  battSave();
}

void batteryUpdate() {
  if (!inaOk || isnan(inaCurrentmA)) return;
  unsigned long now = millis();
  float dt = (now - battLastUpdateMs) / 1000.0f;
  battLastUpdateMs = now;
  if (dt <= 0) return;
  if (dt > 10.0f) dt = 10.0f;

  battLoadV = inaBusV + inaShuntmV / 1000.0f;
  float P_mW = battLoadV * inaCurrentmA;
  if (inaCurrentmA > battPeakmA) battPeakmA = inaCurrentmA;

  float dWh = (P_mW / 1000.0f) * (dt / 3600.0f);
  battRemWh -= dWh;
  battUsedWh += dWh;

  float socV = socFromVoltage(battLoadV + (inaCurrentmA / 1000.0f) * BATT_R_INT_OHM);
  if ((socV > 90.0f || socV < 20.0f) && fabs(inaCurrentmA) < 1000.0f) {
    battRemWh += BATT_CORR_PER_S * dt * (BATT_CAP_WH * socV / 100.0f - battRemWh);
  }
  battRemWh = constrain(battRemWh, 0.0f, BATT_CAP_WH);
  battSocPct = battRemWh / BATT_CAP_WH * 100.0f;

  if (!battAvgInit) { battAvgPmW = P_mW; battAvgInit = true; }
  battAvgPmW += (1.0f - expf(-dt / BATT_AVG_TAU_S)) * (P_mW - battAvgPmW);
  battHoursLeft = (battAvgPmW > 20.0f) ? battRemWh / (battAvgPmW / 1000.0f) : NAN;

  if ((now - battLastSaveMs) / 1000UL >= BATT_SAVE_EVERY_S && fabs(battRemWh - battLastSavedWh) >= 0.05f) battSave();
}

// ===================================================================
// FAN TACH (3-pin PC fan, tach wire to TACH_PIN)
// ===================================================================
volatile uint32_t tachPulses = 0;
volatile int64_t  tachLastEdgeUs = 0;
float fanRpm = NAN;
bool  fanStalled = false;

void IRAM_ATTR tachISR() {
  int64_t t = esp_timer_get_time();
  if (t - tachLastEdgeUs > TACH_DEBOUNCE_US) {
    tachPulses = tachPulses + 1;   // not "++": incrementing a volatile is deprecated in C++20
    tachLastEdgeUs = t;
  }
}

void updateFanRpm(unsigned long nowMs) {
  if (!FAN_ENABLED) { fanRpm = NAN; fanStalled = false; return; }
  static unsigned long lastMs = 0;
  static uint32_t lastCount = 0;
  uint32_t count = tachPulses;
  if (lastMs != 0 && nowMs > lastMs) {
    float dtMs = (float)(nowMs - lastMs);
    fanRpm = ((float)(count - lastCount) * 60000.0f) / (FAN_PULSES_PER_REV * dtMs);
  }
  lastMs = nowMs;
  lastCount = count;
  fanStalled = (nowMs > FAN_STALL_GRACE_MS) && !isnan(fanRpm) && (fanRpm < FAN_STALL_RPM);
}

// ===================================================================
// SHARED TELEMETRY (loop() writes, the upload task reads)
// ===================================================================
LiquidCrystal lcd(27, 26, 25, 33, 14, 13);
Adafruit_BMP3XX bmp;
bool bmpOk = false;

float tempC = NAN, pressHPa = NAN, altitudeM = NAN;
float thermRaw = 0, thermFiltered = 0;
float rfRawDBm = 0, rfFiltered = 0;
int   thermCounts = 0, rfCounts = 0;

rqs::Engine rqsEng;                       // reception quality score (about 6 KB); see the RQS section below

Telemetry tel;
SemaphoreHandle_t telMutex;
volatile bool touchEventPending = false;   // a press or release that should reach the website at once

void refreshTelemetry() {
  xSemaphoreTake(telMutex, portMAX_DELAY);
  tel.tempC = tempC;  tel.pressHPa = pressHPa;  tel.altitudeM = altitudeM;
  tel.thermRaw = thermRaw;  tel.thermFilt = thermFiltered;
  tel.rfRawDBm = rfRawDBm;  tel.rfFilt = rfFiltered;
  tel.capTdUs = capTdUs;  tel.capDeltaUs = capDeltaUs;  tel.capPF = capPF;
  tel.capPressed = capPressed;  tel.capFault = capFault;  tel.touchCount = (uint32_t)capPressCount;
  tel.inaOk = inaOk;
  tel.battV = battLoadV;  tel.battmA = inaCurrentmA;  tel.battmW = inaPowermW;
  tel.battSocPct = battSocPct;  tel.battWhLeft = battRemWh;  tel.battHoursLeft = battHoursLeft;
  tel.battPeakmA = battPeakmA;
  tel.fanRpm = fanRpm;  tel.fanStalled = fanStalled;
  tel.rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  tel.uptimeS = (uint32_t)(millis() / 1000UL);
  tel.rq = rqsEng.result();
  xSemaphoreGive(telMutex);
}

// Called right after every capacitive update so a press reaches Firebase quickly.
void refreshTouchTelemetry() {
  xSemaphoreTake(telMutex, portMAX_DELAY);
  tel.capPressed = capPressed;
  tel.capFault = capFault;
  tel.touchCount = (uint32_t)capPressCount;
  tel.capTdUs = capTdUs;
  tel.capDeltaUs = capDeltaUs;
  xSemaphoreGive(telMutex);
}

// ===================================================================
// RECEPTION QUALITY SCORE (rqs.h, SPEC.md section 14)
// ===================================================================
// rqs.h does the work and is tested on a PC. This part feeds it one sample every READ_INTERVAL_MS, keeps the clock
// (NTP) and takes the pass schedule from the upload task. Temperature comes from the BMP388 only, never the thermistors.
unsigned long rqsLastMs = 0;
bool warmCsv = false;                 // serial `w`: CSV for the warm-up test that fits RQS_ALPHA_T_DB_PER_C

// Shared with the upload task, under telMutex: it parses a fetched schedule into schedNext, loop() copies it in.
rqs::Schedule schedNext;
volatile bool schedNextReady = false;
volatile int schedLastCode = 0;       // last fetch: HTTP code, -1 file unreadable, -2 too big, 0 none yet
volatile unsigned long schedLastOkMs = 0;

// Unix seconds from NTP, or 0 while the clock is not set.
int64_t utcNow() {
  int64_t t = (int64_t)time(nullptr);
  return t >= RQS_CLOCK_SET_UTC ? t : 0;
}

// Every READ_INTERVAL_MS, after the RF read and batteryUpdate().
void rqsUpdate() {
  if (schedNextReady) {
    xSemaphoreTake(telMutex, portMAX_DELAY);
    rqsEng.setSchedule(schedNext);
    schedNextReady = false;
    xSemaphoreGive(telMutex);
  }
  unsigned long now = millis();
  rqs::Input in;
  in.dtS    = rqsLastMs ? (now - rqsLastMs) / 1000.0f : READ_INTERVAL_MS / 1000.0f;
  in.rfDbm  = rfFiltered;
  in.tempC  = tempC;                  // raw bmp.temperature: NaN when bmpOk is false or the read failed
  in.socPct = inaOk ? battSocPct : NAN;
  in.nowUtc = utcNow();
  rqsLastMs = now;
  const rqs::Result& r = rqsEng.step(in);
  if (warmCsv) {
    Serial.print(F("WARM,")); Serial.print(now);
    Serial.print(','); Serial.print(rfFiltered, 3);
    Serial.print(','); Serial.print(rfRawDBm, 3);
    Serial.print(','); Serial.print(tempC, 3);
    Serial.print(','); Serial.print(r.tempC, 3);       // nan when the BMP388 sample was rejected
    Serial.print(','); Serial.println(r.rfComp, 3);
  }
}

// ===================================================================
// FIREBASE (runs in its own task on core 0, so TLS never disturbs the cap timing on core 1)
// ===================================================================
// Data layout (see ../README.md):
//   /station/latest   overwritten every LATEST_INTERVAL_MS
//   /station/history  one record appended every HISTORY_INTERVAL_MS (push id keys)
//   /station/touch    written at once when the press state changes
//   /station/meta     written once per boot
// It also fetches the pass schedule for the RQS (needs only WiFi, not Firebase).
WiFiClientSecure authClient;
WiFiClientSecure dbClient;
WiFiClientSecure schedClient;         // pass schedule only: connects every few hours and closes at once
String fbIdToken;
unsigned long fbTokenMs = 0;
bool fbAuthOk = false;
volatile int fbLastCode = 0;          // last HTTP code, shown on the LCD and Serial
volatile bool fbOnline = false;       // last write succeeded

String jnum(float v, int decimals) {
  if (isnan(v) || isinf(v)) return String("null");
  return String(v, decimals);
}

String jbool(bool b) { return String(b ? "true" : "false"); }

// Pull the string value of "key" out of a flat JSON reply without a JSON library.
String jsonString(const String& payload, const char* key) {
  String pat = String("\"") + key + "\"";
  int k = payload.indexOf(pat);
  if (k < 0) return String("");
  int colon = payload.indexOf(':', k + pat.length());
  if (colon < 0) return String("");
  int q1 = payload.indexOf('"', colon + 1);
  if (q1 < 0) return String("");
  int q2 = payload.indexOf('"', q1 + 1);
  if (q2 < 0) return String("");
  return payload.substring(q1 + 1, q2);
}

// withParts adds the four RQS parts, which go in /station/latest only (SPEC 14.12).
String buildTelemetryJson(const Telemetry& t, bool withParts) {
  String s;
  s.reserve(800);
  s += "{\"ts\":{\".sv\":\"timestamp\"}";
  s += ",\"tempC\":" + jnum(t.tempC, 2);
  s += ",\"pressHPa\":" + jnum(t.pressHPa, 2);
  s += ",\"altM\":" + jnum(t.altitudeM, 1);
  s += ",\"thermRaw\":" + jnum(t.thermRaw, 2);
  s += ",\"thermFilt\":" + jnum(t.thermFilt, 2);
  s += ",\"rfRawDbm\":" + jnum(t.rfRawDBm, 2);
  s += ",\"rfFiltDbm\":" + jnum(t.rfFilt, 2);
  s += ",\"capTdUs\":" + jnum(t.capTdUs, 2);
  s += ",\"capDeltaUs\":" + jnum(t.capDeltaUs, 2);
  s += ",\"capPF\":" + jnum(t.capPF, 1);
  s += ",\"touched\":" + jbool(t.capPressed);
  s += ",\"touchCount\":" + String(t.touchCount);
  s += ",\"capFault\":" + jbool(t.capFault);
  s += ",\"battV\":" + (t.inaOk ? jnum(t.battV, 3) : String("null"));
  s += ",\"battmA\":" + (t.inaOk ? jnum(t.battmA, 1) : String("null"));
  s += ",\"battmW\":" + (t.inaOk ? jnum(t.battmW, 0) : String("null"));
  s += ",\"battSocPct\":" + (t.inaOk ? jnum(t.battSocPct, 1) : String("null"));
  s += ",\"battWhLeft\":" + (t.inaOk ? jnum(t.battWhLeft, 2) : String("null"));
  s += ",\"battHoursLeft\":" + (t.inaOk ? jnum(t.battHoursLeft, 2) : String("null"));
  s += ",\"battPeakmA\":" + (t.inaOk ? jnum(t.battPeakmA, 1) : String("null"));
  s += ",\"fanRpm\":" + jnum(t.fanRpm, 0);
  s += ",\"fanStalled\":" + jbool(t.fanStalled);
  s += ",\"rssi\":" + String(t.rssi);
  s += ",\"uptimeS\":" + String(t.uptimeS);
  char rq[360];
  if (rqs::appendJson(t.rq, withParts, rq, sizeof rq) > 0) s += rq;
  s += "}";
  return s;
}

// Pass schedule (SPEC 14.8): data/passes.json from the website, about 3 KB. The certificate is checked against the
// core's built-in bundle, unlike the two Firebase connections.
bool fetchSchedule() {
  static rqs::Schedule parsed;        // about 2.5 KB, kept off the task stack
  HTTPClient http;
  schedClient.useBuiltinCACertBundle();
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(schedClient, PASSES_URL)) { schedLastCode = -100; return false; }
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    if (http.getSize() > 16384) {
      code = -2;
    } else {
      String body = http.getString();
      if (body.length() <= 16384 && rqs::parseSchedule(body.c_str(), body.length(), parsed)) {
        xSemaphoreTake(telMutex, portMAX_DELAY);
        schedNext = parsed;
        schedNextReady = true;
        xSemaphoreGive(telMutex);
        schedLastOkMs = millis();
        ok = true;
      } else {
        code = -1;
      }
    }
  }
  schedLastCode = code;
  http.end();
  schedClient.stop();
  Serial.print(F("Pass schedule fetch -> "));
  if (ok) { Serial.print(parsed.n); Serial.println(F(" passes")); }
  else { Serial.print(F("failed, code ")); Serial.println(code); }
  return ok;
}

bool fbSignIn() {
  HTTPClient http;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;
  authClient.setInsecure();   // TODO: pin the Google root certificate instead of skipping verification
  http.setTimeout(8000);
  if (!http.begin(authClient, url)) return false;
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"email\":\"") + FIREBASE_USER_EMAIL + "\",\"password\":\"" + FIREBASE_USER_PASSWORD +
                "\",\"returnSecureToken\":true}";
  int code = http.POST(body);
  bool ok = false;
  if (code == 200) {
    String reply = http.getString();
    String tok = jsonString(reply, "idToken");
    if (tok.length() > 100) {          // keep the old token if this reply is unusable
      fbIdToken = tok;
      fbTokenMs = millis();
      ok = true;
    }
  }
  fbLastCode = code;
  Serial.print(F("Firebase sign-in -> HTTP ")); Serial.println(code);
  http.end();
  return ok;
}

// method is "PUT" or "POST". Returns the HTTP code (200 means success).
int fbSend(const char* method, const char* path, const String& json) {
  String url = String("https://") + FIREBASE_DB_HOST + path + ".json?auth=" + fbIdToken;
  HTTPClient http;
  dbClient.setInsecure();     // TODO: pin the Google root certificate
  http.setReuse(true);        // keep the TLS connection open between writes
  http.setTimeout(6000);
  if (!http.begin(dbClient, url)) return -100;
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, json);
  http.end();
  fbLastCode = code;
  fbOnline = (code == 200);
  if (code == 401 || code == 403) fbAuthOk = false;   // token expired or rules refused it: sign in again
  return code;
}

void uploadTask(void* arg) {
  unsigned long lastLatest = 0, lastHistory = 0, lastSignInTry = 0, lastSchedTry = 0;
  bool metaSent = false, schedOk = false;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      fbOnline = false;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    unsigned long now = millis();

    // The pass schedule needs only WiFi, so it is fetched before (and without) the Firebase sign-in.
    if (PASSES_URL[0] &&
        (lastSchedTry == 0 || now - lastSchedTry >= (schedOk ? RQS_SCHED_EVERY_S : RQS_SCHED_RETRY_S) * 1000UL)) {
      lastSchedTry = now;
      schedOk = fetchSchedule();
    }

    if (!fbAuthOk || (now - fbTokenMs) > TOKEN_REFRESH_MS) {
      // At most one sign-in attempt per SIGNIN_MIN_GAP_MS, even if the database keeps refusing the token.
      if (lastSignInTry != 0 && (now - lastSignInTry) < SIGNIN_MIN_GAP_MS) {
        if (!fbAuthOk) { vTaskDelay(500 / portTICK_PERIOD_MS); continue; }
      } else {
        lastSignInTry = now;
        if (fbSignIn()) {
          fbAuthOk = true;
        } else if (!fbAuthOk || (now - fbTokenMs) > TOKEN_EXPIRY_MS) {
          fbAuthOk = false;            // no valid token left
          fbOnline = false;
          vTaskDelay(500 / portTICK_PERIOD_MS);
          continue;
        }                              // otherwise keep using the old token until it really expires
      }
    }

    Telemetry t;
    xSemaphoreTake(telMutex, portMAX_DELAY);
    t = tel;
    xSemaphoreGive(telMutex);

    if (!metaSent) {
      String meta = String("{\"fw\":\"") + FW_VERSION + "\",\"bootTs\":{\".sv\":\"timestamp\"},\"rqsAlphaT\":" +
                    String(RQS_ALPHA_T_DB_PER_C, 4) + "}";
      if (fbSend("PUT", "/station/meta", meta) == 200) metaSent = true;
    }

    if (touchEventPending) {
      touchEventPending = false;
      String touch = String("{\"state\":") + jbool(t.capPressed) + ",\"count\":" + String(t.touchCount) +
                     ",\"ts\":{\".sv\":\"timestamp\"}}";
      fbSend("PUT", "/station/touch", touch);
    }

    if (now - lastLatest >= LATEST_INTERVAL_MS) {
      lastLatest = now;
      fbSend("PUT", "/station/latest", buildTelemetryJson(t, true));
    }
    if (now - lastHistory >= HISTORY_INTERVAL_MS) {
      lastHistory = now;
      fbSend("POST", "/station/history", buildTelemetryJson(t, false));
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ===================================================================
// LCD AND SERIAL
// ===================================================================
unsigned long lastRead = 0, lastCap = 0, lastIna = 0, lastLcd = 0, lastWifiTry = 0;
int screenIndex = 0;
bool lastReportedPressed = false;

void lcdLine(int row, const String& text) {
  String s = text;
  while (s.length() < 16) s += ' ';
  lcd.setCursor(0, row);
  lcd.print(s.substring(0, 16));
}

// Five pages (SPEC 15.1). Page 0, RF and the score, is the one shown at power on; each press goes to the next.
void drawScreen() {
  char a[24], b[24], t[12];
  switch (screenIndex) {
    case 0: {   // RF strength and the reception quality score
      const rqs::Result& q = rqsEng.result();
      if (q.scored) snprintf(a, sizeof(a), "RF%6.1f %+5.1fdB", rfFiltered, q.excessDb);
      else          snprintf(a, sizeof(a), "RF %.1f dBm", rfFiltered);
      if (!q.scored) snprintf(b, sizeof(b), "RQS warming up");
      else           snprintf(b, sizeof(b), "RQS %.0f %s", q.rqs,           // label and tag take turns every 2 s
                              (millis() / 2000) % 2 ? rqs::tagShort(q.tag) : rqs::labelText(q.label));
      lcdLine(0, a); lcdLine(1, b);
      break;
    }
    case 1:     // temperatures
      if (isnan(tempC))         snprintf(a, sizeof(a), "BMP388     --");
      else                      snprintf(a, sizeof(a), "BMP388 %6.1f C", tempC);
      if (isnan(thermFiltered)) snprintf(b, sizeof(b), "Therm      --");
      else                      snprintf(b, sizeof(b), "Therm  %6.1f C", thermFiltered);
      lcdLine(0, a); lcdLine(1, b);
      break;
    case 2:     // pressure and altitude (BMP388)
      if (isnan(pressHPa)) { lcdLine(0, "P -- hPa"); lcdLine(1, "Alt -- m"); break; }
      snprintf(a, sizeof(a), "P %7.1f hPa", pressHPa);
      snprintf(b, sizeof(b), "Alt %5.0f m", altitudeM);
      lcdLine(0, a); lcdLine(1, b);
      break;
    case 3:     // fan
      if (isnan(fanRpm)) snprintf(a, sizeof(a), "Fan -- RPM");
      else               snprintf(a, sizeof(a), "Fan %4.0f RPM", fanRpm);
      lcdLine(0, a);
      lcdLine(1, !FAN_ENABLED ? "off" : isnan(fanRpm) ? "--" : fanStalled ? "STALLED" : "ok");
      break;
    case 4:     // battery current and time left
      if (!inaOk) { lcdLine(0, "INA219 missing"); lcdLine(1, "check wiring"); break; }
      if (fabs(inaCurrentmA) >= 1000.0f) snprintf(a, sizeof(a), "I %6.3f A", inaCurrentmA / 1000.0f);
      else                                snprintf(a, sizeof(a), "I %6.1f mA", inaCurrentmA);
      if (isnan(battHoursLeft))           snprintf(t, sizeof(t), "--");
      else if (battHoursLeft >= 100.0f)   snprintf(t, sizeof(t), ">99h");
      else snprintf(t, sizeof(t), "%dh%02dm", (int)battHoursLeft, (int)((battHoursLeft - (int)battHoursLeft) * 60.0f));
      snprintf(b, sizeof(b), "%3.0f%% %s %s", battSocPct,
               (battLoadV < BATT_LOW_VOLTS || battSocPct < BATT_LOW_PERCENT) ? "LOW " : "left", t);
      lcdLine(0, a); lcdLine(1, b);
      break;
  }
}

void printToSerial() {
  Serial.print(F("BMP388  Temp: ")); Serial.print(tempC, 2);
  Serial.print(F(" C  Pressure: ")); Serial.print(pressHPa, 2);
  Serial.print(F(" hPa  Alt: ")); Serial.print(altitudeM, 1);
  Serial.println(F(" m"));

  Serial.print(F("Therm   counts: ")); Serial.print(thermCounts);
  Serial.print(F("  Raw: ")); Serial.print(thermRaw, 2);
  Serial.print(F(" C  Filtered: ")); Serial.print(thermFiltered, 2);
  Serial.println(F(" C"));

  Serial.print(F("RF      counts: ")); Serial.print(rfCounts);
  Serial.print(F("  Raw: ")); Serial.print(rfRawDBm, 2);
  Serial.print(F(" dBm  Filtered: ")); Serial.print(rfFiltered, 2);
  Serial.println(F(" dBm"));

  const rqs::Result& q = rqsEng.result();
  Serial.print(F("RQS     "));
  if (q.scored) {
    Serial.print(q.rqs, 1); Serial.print(' '); Serial.print(rqs::labelText(q.label));
    Serial.print(F("  excess ")); Serial.print(q.excessDb, 2);
    Serial.print(F(" dB  baseline ")); Serial.print(q.baselineDbm, 2); Serial.print(F(" dBm"));
    if (q.detected) Serial.print(F("  DETECTED"));
  } else {
    Serial.print(F("warming up (")); Serial.print(q.training); Serial.print(F(" of ")); Serial.print(RQS_MIN_TRAIN_N);
    Serial.print(')');
  }
  Serial.print(F("  tag ")); Serial.println(rqs::tagText(q.tag));

  Serial.print(F("Cap     td: ")); Serial.print(capTdUs, 2);
  Serial.print(F(" us  delta: ")); Serial.print(capDeltaUs, 2);
  Serial.print(F(" us  C: ")); Serial.print(capPF, 1);
  Serial.print(F(" pF  presses: ")); Serial.print(capPressCount);
  Serial.print(F("  ")); Serial.println(capFault ? F("FAULT") : (capPressed ? F("PRESSED") : F("idle")));

  if (inaOk) {
    Serial.print(F("Battery V: ")); Serial.print(battLoadV, 2);
    Serial.print(F(" V  I: ")); Serial.print(inaCurrentmA, 1);
    Serial.print(F(" mA  P: ")); Serial.print(inaPowermW, 0);
    Serial.print(F(" mW  SOC: ")); Serial.print(battSocPct, 1);
    Serial.print(F("%  left: ")); Serial.print(battRemWh, 2); Serial.print(F(" Wh"));
    if (!isnan(battHoursLeft)) { Serial.print(F(" = ")); Serial.print(battHoursLeft, 1); Serial.print(F(" h")); }
    Serial.print(F("  used: ")); Serial.print(battUsedWh, 3); Serial.println(F(" Wh"));
  }

  Serial.print(F("Fan     RPM: ")); Serial.print(fanRpm, 0);
  Serial.println(fanStalled ? F("  STALLED") : F(""));

  Serial.print(F("Link    WiFi: ")); Serial.print(WiFi.status() == WL_CONNECTED ? "up" : "down");
  Serial.print(F("  RSSI: ")); Serial.print(WiFi.RSSI());
  Serial.print(F(" dBm  Firebase: ")); Serial.print(fbOnline ? "OK" : "not ok");
  Serial.print(F("  last HTTP: ")); Serial.println(fbLastCode);
  Serial.println();
}

// CSV lines for the "reading changes on the microcontroller" figure.
void printCapStream(unsigned long ms) {
  Serial.print(F("CAP,")); Serial.print(ms);
  Serial.print(','); Serial.print(capTdUs, 2);
  Serial.print(','); Serial.print(capDeltaUs, 2);
  Serial.print(','); Serial.print(capPF, 1);
  Serial.print(','); Serial.println(capPressed ? 1 : 0);
}

void capRawDump() {
  Serial.println(F("Cap raw delays (us), one per cycle:"));
  for (int i = 0; i < CAP_CYCLES; i++) {
    float x = capMeasureOnceUs();
    Serial.print(x, 2); Serial.print(i < CAP_CYCLES - 1 ? ", " : "\n");
  }
  Serial.println(F("-1 = comparator already high before the edge, -2 = timeout."));
}

// Isolation and gain check. Compare each pin voltage with the DMM at the sensor outputs.
void muxTest() {
  Serial.println(F("Mux test:"));
  struct Row { const char* name; MuxChannel ch; };
  Row rows[] = { {"both blocked", MUX_NONE}, {"thermistor only", MUX_THERM}, {"RF only", MUX_RF} };
  for (int i = 0; i < 3; i++) {
    int counts = muxReadCounts(rows[i].ch);
    float vPin = countsToPinVolts(counts);
    Serial.print("  "); Serial.print(rows[i].name);
    Serial.print(": counts "); Serial.print(counts);
    Serial.print("  pin "); Serial.print(vPin, 3); Serial.print(" V");
    if (rows[i].ch == MUX_THERM) { Serial.print("  sensor side "); Serial.print(vPin / MUX_GAIN_T, 3); Serial.print(" V"); }
    if (rows[i].ch == MUX_RF)    { Serial.print("  sensor side "); Serial.print(vPin / MUX_GAIN_R, 3); Serial.print(" V"); }
    Serial.println();
  }
  Serial.println(F("  'both blocked' should be near 0 V. A big number means a shunt transistor is not turning on."));
}

void printRqsReport() {
  Serial.println(F("--- Reception quality score ---"));
  rqsEng.debugLines([](const char* line, void* ctx) { (void)ctx; Serial.println(line); }, nullptr, utcNow());
  Serial.print(F("Schedule fetch: "));
  if (!PASSES_URL[0]) {
    Serial.println(F("PASSES_URL is empty in secrets.h (set it once GitHub Pages is enabled)"));
  } else {
    Serial.print(F("last result ")); Serial.print(schedLastCode);
    Serial.print(F(" (200 ok, -1 file unreadable, -2 too big, 0 not tried yet)"));
    if (schedLastOkMs) { Serial.print(F(", last success ")); Serial.print((millis() - schedLastOkMs) / 60000UL); Serial.print(F(" min ago")); }
    Serial.println();
  }
  Serial.print(F("INA219: "));
  Serial.print(INA219_CALIBRATED ? F("calibrated") : F("uncalibrated (PLACEHOLDER gain and offset)"));
  Serial.print(F(", averaging restored ")); Serial.print(inaConfigRewrites); Serial.println(F(" times"));
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    if (c == 'b') {
      Serial.println(F("Recapturing cap baseline, hands off the pad..."));
      if (capCaptureBaseline()) { Serial.print(F("Baseline: ")); Serial.print(capBaselineUs, 2); Serial.println(F(" us")); }
      else Serial.println(F("Baseline FAILED."));
    } else if (c == 's') {
      capStream = !capStream;
      Serial.println(capStream ? F("Cap stream ON") : F("Cap stream OFF"));
    } else if (c == 'c') {
      capRawDump();
    } else if (c == 'm') {
      muxTest();
    } else if (c == 'f') {
      battRemWh = BATT_CAP_WH; battUsedWh = 0; battSave();
      Serial.println(F("Battery set to FULL."));
    } else if (c == 'r') {
      battPeakmA = 0;
      Serial.println(F("Peak current reset."));
    } else if (c == 'x') {
      prefs.remove("remWh");
      Serial.println(F("Saved battery value cleared. Reboot to restart from the voltage reading."));
    } else if (c == 't') {
      rqsEng.testMode = !rqsEng.testMode;
      Serial.println(rqsEng.testMode ? F("RQS TEST MODE on: pass part fixed at 0.5, tag TEST. A restart turns it off.")
                                     : F("RQS TEST MODE off."));
    } else if (c == 'q') {
      printRqsReport();
    } else if (c == 'w') {
      warmCsv = !warmCsv;
      if (warmCsv) Serial.println(F("WARM,ms,rfFilt,rfRaw,tempRaw,tempSmooth,rfComp"));
      else Serial.println(F("Warm-up CSV OFF"));
    }
  }
}

// ===================================================================
// SENSOR READS
// ===================================================================
void readAllSensors() {
  bool ok = bmpOk && bmp.performReading();
  tempC     = ok ? bmp.temperature : NAN;
  pressHPa  = ok ? (bmp.pressure / 100.0) : NAN;
  altitudeM = ok ? bmp.readAltitude(SEALEVELPRESSURE_HPA) : NAN;

  // Both raw sensors through the one ADC pin, undoing the mux divider per channel.
  thermCounts = muxReadCounts(MUX_THERM);
  float vTherm = countsToPinVolts(thermCounts) / MUX_GAIN_T;
  thermRaw = thermistorVoltsToCelsius(vTherm);
  thermFiltered = thermKalman.update(thermRaw);

  rfCounts = muxReadCounts(MUX_RF);
  float vRf = countsToPinVolts(rfCounts) / MUX_GAIN_R;
  rfRawDBm = rfVoltsToDBm(vRf);
  rfFiltered = rfKalman.update(rfRawDBm);

  readINA219();
  batteryUpdate();
  rqsUpdate();
  updateFanRpm(millis());
  refreshTelemetry();
}

// ===================================================================
// SETUP AND LOOP
// ===================================================================
void setup() {
  // Block both mux channels before anything else.
  pinMode(BLOCK_T_PIN, OUTPUT);
  pinMode(BLOCK_R_PIN, OUTPUT);
  muxSelect(MUX_NONE);

  pinMode(CAP_DRIVE_PIN, OUTPUT);
  REG_WRITE(GPIO_OUT_W1TC_REG, CAP_DRIVE_MASK);
  pinMode(CAP_COMP_PIN, INPUT);
  capCpuMHz = getCpuFrequencyMhz();

  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  telMutex = xSemaphoreCreateMutex();
  memset(&tel, 0, sizeof(tel));
  tel.tempC = tel.pressHPa = tel.altitudeM = NAN;
  tel.capTdUs = tel.capDeltaUs = tel.capPF = NAN;
  tel.fanRpm = NAN;

  lcd.begin(16, 2);
  lcdLine(0, "V-Dipole Station");
  lcdLine(1, "Starting...");

  bmpOk = bmp.begin_I2C(BMP388_I2C_ADDR);
  if (bmpOk) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp.setOutputDataRate(BMP3_ODR_50_HZ);
    Serial.println(F("BMP388 OK"));
  } else {
    // Not fatal: everything else keeps running and the BMP388 fields are sent as null.
    Serial.println(F("BMP388 not found at 0x77, continuing without it."));
    lcdLine(1, "BMP388 missing");
    delay(1500);
  }

  inaOk = ina219.begin();
  if (inaOk) {
    ina219.setCalibration_32V_1A();   // 32 V, 1 A range, assumes the 0.1 ohm (R100) shunt
    ina219WriteConfig();              // then 128-sample averaging
    delay(150);                       // one full averaged conversion (bus and shunt take 69 ms each)
    Serial.println(F("INA219 OK"));
    readINA219();
  } else {
    Serial.println(F("INA219 not found, continuing without it."));
  }
  batteryInit();

  // Fan tach: open-collector output, one falling edge per pulse. The external 10k pull-up sits on the fan side of the
  // 10k series resistor. No internal pull-up: it would sit on the GPIO side and lift a low to about 0.6 V.
  if (FAN_ENABLED) {
    pinMode(TACH_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, FALLING);
  }

  thermKalman.init(20.0, 0.01, 0.5);
  rfKalman.init(-80.0, 0.01, 1.0);

  lcdLine(0, "Cap baseline");
  lcdLine(1, "Hands off pad...");
  delay(1000);
  if (capCaptureBaseline()) {
    Serial.print(F("Cap baseline: ")); Serial.print(capBaselineUs, 2); Serial.println(F(" us"));
  } else {
    Serial.println(F("Cap baseline FAILED. Check CAP_DRIVE, CAP_COMP, the 1M resistor, and CAP_REF."));
  }

  lcdLine(0, "Connecting WiFi");
  lcdLine(1, "");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // UTC for the pass schedule; syncs once WiFi is up
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi not connected yet. It keeps retrying in the background.");
  }

  readAllSensors();

  // Upload task on core 0 (the Arduino loop() runs on core 1). 16 KB of stack for TLS.
  xTaskCreatePinnedToCore(uploadTask, "upload", 16384, NULL, 1, NULL, 0);

  Serial.println(F("Commands: b = new cap baseline, s = toggle cap stream, c = cap raw dump, m = mux test,"));
  Serial.println(F("          f = battery is full, r = reset peak current, x = clear saved battery value,"));
  Serial.println(F("          t = RQS test mode, q = RQS report, w = warm-up CSV"));
  Serial.println(F("Setup complete.\n"));
}

void loop() {
  unsigned long now = millis();

  handleSerial();

  if (now - lastCap >= CAP_INTERVAL_MS) {
    lastCap = now;
    capUpdate();
    refreshTouchTelemetry();
    if (capStream) printCapStream(now);
    if (capPressed != lastReportedPressed) {          // press or release: tell the website at once
      lastReportedPressed = capPressed;
      touchEventPending = true;
    }
    if (capPressEvent) {
      capPressEvent = false;
      screenIndex = (screenIndex + 1) % SCREEN_COUNT;   // the user input: a press changes the page
      Serial.print(F("PRESS #")); Serial.print(capPressCount);
      Serial.print(F("  delta ")); Serial.print(capDeltaUs, 1); Serial.println(F(" us"));
      drawScreen();
    }
  }

  if (now - lastIna >= INA_SAMPLE_MS) {
    lastIna = now;
    inaSample();
  }

  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;
    readAllSensors();
    printToSerial();
  }

  if (now - lastLcd >= LCD_REFRESH_MS) {
    lastLcd = now;
    drawScreen();
  }

  // The ESP32 reconnects by itself, this is a backstop if it gives up.
  if (WiFi.status() != WL_CONNECTED && now - lastWifiTry > 30000) {
    lastWifiTry = now;
    WiFi.reconnect();
  }
}
