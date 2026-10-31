import { isNum } from "./format.js";

// Typical 18650 open-circuit voltage per cell against charge, the same table as the firmware.
const CELL_V = [4.2, 4.15, 4.11, 4.08, 4.02, 3.98, 3.95, 3.91, 3.87, 3.85, 3.84,
  3.82, 3.8, 3.79, 3.77, 3.75, 3.73, 3.71, 3.69, 3.61, 3.3, 3.0];
const CELL_PCT = [100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50,
  45, 40, 35, 30, 25, 20, 15, 10, 5, 2, 0];

// Open-circuit pack voltage a battery at this charge would show.
export function packVoltsAtSoc(pct, cells) {
  if (!isNum(pct)) return null;
  const p = Math.min(100, Math.max(0, pct));
  for (let i = 0; i < CELL_PCT.length - 1; i++) {
    if (p >= CELL_PCT[i + 1]) {
      const f = (p - CELL_PCT[i + 1]) / (CELL_PCT[i] - CELL_PCT[i + 1]);
      return cells * (CELL_V[i + 1] + f * (CELL_V[i] - CELL_V[i + 1]));
    }
  }
  return cells * CELL_V[CELL_V.length - 1];
}

// Color band for the battery widget (SPEC D15): green above 35%, amber 20 to 35%, red below 20%.
export function batteryBand(socPct, volts, { amberAtOrBelowPct, redBelowPct, lowVolts }) {
  if (!isNum(socPct)) return "none";
  if (socPct < redBelowPct || (isNum(volts) && volts < lowVolts)) return "red";
  if (socPct <= amberAtOrBelowPct) return "amber";
  return "green";
}

export function isLowBattery(socPct, volts, { redBelowPct, lowVolts }) {
  return (isNum(socPct) && socPct < redBelowPct) || (isNum(volts) && volts < lowVolts);
}
