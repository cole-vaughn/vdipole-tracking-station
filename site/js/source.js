// Live data source: Firebase Realtime Database, read-only, no sign-in, no API key.
// The test and dev servers swap this file for tests/fixtures/fixture-source.js, so the shipped site
// never contains test data.
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.19.0/firebase-app.js";
import {
  getDatabase, ref, onValue, query, orderByChild, startAt, limitToLast, get, goOffline, goOnline,
} from "https://www.gstatic.com/firebasejs/12.19.0/firebase-database.js";

export function createSource(config) {
  if (!config.databaseURL) return null;
  const db = getDatabase(initializeApp({ databaseURL: config.databaseURL }));
  const listen = (path, cb, onError) => onValue(ref(db, path), (snap) => cb(snap.val()), onError);

  return {
    label: "Firebase Realtime Database",
    // Small nodes only. Never a listener on /station/history: that would download all of it.
    onLatest: (cb, onError) => listen("station/latest", cb, onError),
    onTouch: (cb, onError) => listen("station/touch", cb, onError),
    onMeta: (cb, onError) => listen("station/meta", cb, onError),
    onConnection: (cb) => listen(".info/connected", (v) => cb(v === true)),
    onClockOffset: (cb) => listen(".info/serverTimeOffset", (v) => cb(typeof v === "number" ? v : 0)),

    // History is a query, never a listener. The database rules only allow this shape of read.
    async fetchHistory(sinceMs, limit) {
      const snap = await get(query(ref(db, "station/history"), orderByChild("ts"), startAt(sinceMs), limitToLast(limit)));
      const out = [];
      snap.forEach((child) => { out.push(child.val()); });
      return out;
    },

    // Close the connection while the tab is hidden (free plan: 100 connections).
    pause: () => goOffline(db),
    resume: () => goOnline(db),
  };
}
