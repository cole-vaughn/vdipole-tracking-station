// Site configuration. Everything in this file is public: no secrets belong here.
// Every number that has not been measured on the real station is marked "untuned".

export const CONFIG = {
  title: "Satellite V-Dipole Tracking Station",

  // Firebase Realtime Database URL, for example "https://your-project-default-rtdb.firebaseio.com".
  // Public by design (the database allows public read). Empty until the Firebase project exists;
  // the page then shows a calm "not connected yet" state instead of errors.
  databaseURL: "https://vdipole-tracking-station-default-rtdb.firebaseio.com",

  // Rounded to two decimals (about 1 km) on purpose. Never put a street address here.
  station: { label: "Station", lat: 33.92, lon: -84.84, altKm: 0.3 },

  // Station status from the age of the newest record (SPEC D7).
  freshness: { liveMs: 6000, offlineMs: 30000 },

  // v1 shows the first satellite only, but the code works from this list.
  satellites: [
    { noradId: 59051, name: "Meteor-M2-4", downlink: "LRPT near 137.9 MHz" },
  ],
  orbit: {
    file: "data/gp.json",
    staleDays: 7,          // warn when the orbital elements are older than this
    trackMinutes: 50,      // ground track drawn this far back and ahead
    passSearchHours: 24,
    minPassElevationDeg: 0,
    passListCount: 5,      // upcoming passes table
    passListHours: 48,
    lowPassDeg: 15,        // passes peaking below this are marked "low" (weak signal expected)
  },

  // RF visuals use dB above the station's own baseline (rfExcessDb, SPEC F4 and 14.5), never absolute dBm.
  // Full scale matches the score's strength part (RQS_D_FULL_DB in rqs.h).
  rf: { fullScaleDb: 12 },

  // Battery colors (SPEC D15).
  battery: { amberAtOrBelowPct: 35, redBelowPct: 20, lowVolts: 9.5, cells: 3 },

  // Charging is inferred, never measured (SPEC D6, F2). Untuned, so the badge stays hidden until the
  // thresholds are fitted to one logged charge. The live inference still shows in Diagnostics.
  charging: {
    showBadge: false,
    cells: 3,
    rIntOhm: 0.25,         // same placeholder as the firmware
    enterExcessV: 0.25,    // volts above what a discharging pack would show at this load and charge
    exitExcessV: 0.10,     // lower exit threshold: the hysteresis band
    maxDischargeV: 12.65,  // a 3S pack cannot read above this while only discharging
    riseMvPerMin: 15,      // voltage climbing while the load stays steady
    steadyLoadMa: 60,      // current range that still counts as a steady load
    slopeWindowS: 120,
    enterHoldS: 20,        // evidence must hold this long to enter "charging"
    exitHoldS: 60,         // and clear this long to leave it
    minStateS: 60,         // never switch twice within this time
    maxGapS: 30,           // a data gap longer than this resets the detector
  },

  // Measured 2026-10-31: about 1,050 to 1,110 RPM on the 12 V battery, about 650 on USB power (only while flashing).
  // Full speed on the battery spins the drawing at about the visual cap; USB power visibly slower.
  fan: { stallRpm: 200, maxVisualRps: 3, rpmAtMaxVisual: 1200 },

  graphs: {
    ranges: [
      { id: "15m", label: "15 min", ms: 15 * 60e3 },
      { id: "1h", label: "1 h", ms: 60 * 60e3 },
      { id: "6h", label: "6 h", ms: 6 * 60 * 60e3 },
      { id: "24h", label: "24 h", ms: 24 * 60 * 60e3 },
    ],
    defaultRange: "1h",
    maxPoints: 1500,
    historyIntervalS: 10,
    gapMs: 30e3,           // a jump longer than this between points draws a gap
  },
};
