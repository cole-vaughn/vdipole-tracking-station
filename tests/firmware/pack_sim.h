// SYNTHETIC TEST PACK. Nothing in this file is measured.
// A simple model of a 3S Li-ion pack, the station's load and a CC/CV charger, with a known true state of health,
// so the PC tests can check that soh.h recovers it. The curves it makes exist only while the tests run.
#pragma once

#include <math.h>
#include <stdint.h>
#include "../../firmware/station_firmware/soh.h"

constexpr double kPi = 3.14159265358979323846;

struct PackSim {
  // ---------- Truth (SYNTHETIC) ----------
  double sohPct = 94;        // true usable energy, percent of the 26.6 Wh label
  double capAh = 0;          // set by init() from sohPct
  double soc = 0.5;          // true state of charge, 0 to 1
  double r0 = 0.30;          // series resistance, ohm (the firmware assumes 0.25)
  double r1 = 0.06;          // polarization resistance, ohm...
  double tau = 600;          // ...and its time constant, s
  double vrc = 0;            // polarization voltage (positive while discharging)
  double curveMv = 8;        // how far this pack's curve sits from the firmware's table, mV per cell

  // ---------- Station, charger, sensors (SYNTHETIC) ----------
  double loadW = 1.18;       // about 100 mA at 11.8 V, through the buck converter (constant power)
  bool   stationOn = true;
  double tempC = 22;
  bool   plugged = false, chargerDone = false;
  double ccA = 1.0, cvV = 12.6, termA = 0.15;   // the charger also feeds the station, so it sees the load too
  double currentGain = 1.0;  // INA219 gain error to simulate
  double vNoise = 0.0015, iNoiseMa = 0.5;       // noise left after the firmware's 2 s averaging
  double vTerm = 11.5;
  double t = 0;
  double energyOutWh = 0;    // true energy the pack delivered (discharge only)
  bool   dead = false;       // the pack's protection cut the power (under 9.0 V)
  uint64_t seed = 12345;

  void init(double soh, double startSoc) {
    sohPct = soh;
    soc = startSoc;
    capAh = soh / 100.0 * SOH_RATED_WH / meanOcvPack();
    vrc = 0;
    vTerm = ocvPack();
  }

  // The firmware's table, read the other way: charge percent -> volts per cell.
  static double tableCellV(double pct) {
    using namespace soh;
    if (pct >= kCellPct[0]) return kCellV[0];
    for (int i = 0; i < kTableN - 1; i++) {
      if (pct >= kCellPct[i + 1]) {
        double f = (pct - kCellPct[i + 1]) / (kCellPct[i] - kCellPct[i + 1]);
        return kCellV[i + 1] + f * (kCellV[i] - kCellV[i + 1]);
      }
    }
    return kCellV[kTableN - 1];
  }

  // This pack's curve: the table plus a smooth difference that is zero at full and empty.
  double ocvCell(double s) const { return tableCellV(s * 100) + curveMv / 1000.0 * sin(2 * kPi * s); }
  double ocvPack() const { return SOH_CELLS * ocvCell(soc); }
  double meanOcvPack() const {
    double sum = 0;
    const int n = 2000;
    for (int i = 0; i < n; i++) sum += SOH_CELLS * ocvCell((i + 0.5) / n);
    return sum / n;
  }
  // True energy percent: energy from empty up to the present charge, over the energy of a full pack.
  double trueSoe() const {
    double below = 0, total = 0;
    const int n = 2000;
    for (int i = 0; i < n; i++) {
      double s = (i + 0.5) / n, v = ocvCell(s);
      total += v;
      if (s < soc) below += v;
    }
    return 100 * below / total;
  }

  double gauss() {
    // Box-Muller from a small xorshift, so every run is the same.
    auto u = [this]() {
      seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
      return ((seed >> 11) + 0.5) / 9007199254740992.0;
    };
    return sqrt(-2 * log(u())) * cos(2 * kPi * u());
  }

  // Advance dt seconds and return what the firmware would pass to soh.h.
  soh::Sample step(double dt) {
    t += dt;
    double iLoad = (stationOn && !dead) ? loadW / vTerm : 0;
    double iChg = 0;
    if (plugged && !chargerDone) {
      iChg = (cvV - ocvPack() + vrc) / r0 + iLoad;   // what holds the terminals at cvV...
      if (iChg > ccA) iChg = ccA;                    // ...limited to the constant-current stage
      if (iChg < 0) iChg = 0;
      if (iChg < termA) { chargerDone = true; iChg = 0; }
    }
    double iBatt = iLoad - iChg;
    soc -= iBatt * dt / 3600.0 / capAh;
    if (soc > 1) soc = 1;
    if (soc < 0) soc = 0;
    vrc += (iBatt * r1 - vrc) * (1 - exp(-dt / tau));
    vTerm = ocvPack() - iBatt * r0 - vrc;
    if (iBatt > 0) energyOutWh += vTerm * iBatt * dt / 3600.0;
    if (vTerm < 9.0) dead = true;

    soh::Sample s;
    s.dtS = (float)dt;
    s.battV = (float)(vTerm + vNoise * gauss());
    s.mA = (float)(iLoad * 1000 * currentGain + iNoiseMa * gauss());
    s.mW = s.battV * s.mA;
    s.tempC = (float)tempC;
    return s;
  }
};
