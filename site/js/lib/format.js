// Number and time formatting. Null, undefined and NaN always render as "--".

export const isNum = (v) => typeof v === "number" && Number.isFinite(v);

export function fmt(v, digits = 0) {
  return isNum(v) ? v.toFixed(digits) : "--";
}

export function fmtInt(v) {
  return isNum(v) ? Math.round(v).toLocaleString("en-US") : "--";
}

export const cToF = (c) => (c * 9) / 5 + 32;

export function toUnit(c, unit) {
  if (!isNum(c)) return null;
  return unit === "F" ? cToF(c) : c;
}

export function fmtTemp(c, unit, digits = 1) {
  const v = toUnit(c, unit);
  return v === null ? "--" : v.toFixed(digits);
}

export function fmtAge(ms) {
  if (!isNum(ms)) return "--";
  const s = Math.max(0, Math.floor(ms / 1000));
  if (s < 60) return `${s} s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  if (h < 48) return `${h} h ${m % 60} min`;
  return `${Math.floor(h / 24)} d`;
}

export function fmtUptime(sec) {
  if (!isNum(sec) || sec < 0) return "--";
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (d > 0) return `${d} d ${h} h`;
  if (h > 0) return `${h} h ${m} min`;
  if (m > 0) return `${m} min`;
  return `${Math.floor(sec)} s`;
}

// battHoursLeft is null when the draw is tiny; the firmware also caps nothing, so cap the display.
export function fmtHoursLeft(h) {
  if (!isNum(h) || h < 0) return "--";
  if (h >= 100) return ">99h";
  const hh = Math.floor(h);
  const mm = Math.floor((h - hh) * 60);
  return `~${hh}h ${String(mm).padStart(2, "0")}m`;
}

const pad = (n) => String(n).padStart(2, "0");

export function fmtClock(date, utc = false) {
  if (!(date instanceof Date) || Number.isNaN(date.getTime())) return "--";
  return utc
    ? `${pad(date.getUTCHours())}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())}`
    : `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

// "HH:MM" countdown, used for "next pass in".
export function fmtCountdown(ms) {
  if (!isNum(ms)) return "--";
  const totalMin = Math.max(0, Math.round(ms / 60000));
  return `${pad(Math.floor(totalMin / 60))}:${pad(totalMin % 60)}`;
}
