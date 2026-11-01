// Builds and runs the firmware logic tests on this computer (no ESP32 needed).
// Usage: npm run test:firmware [-- -v]
// It also writes a pass schedule with tools/make-passes.mjs from the committed orbit data, so the C++ parser in rqs.h
// is tested on exactly what the deploy workflow publishes.
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { makeSchedule } from "../../tools/make-passes.mjs";

const dir = mkdtempSync(join(tmpdir(), "rqs-test-"));
const exe = join(dir, "rqs_test");
const flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wshadow", "-Werror"];
const run = (cmd, args) => spawnSync(cmd, args, { stdio: "inherit" }).status ?? 1;

const gp = JSON.parse(readFileSync(new URL("../../site/data/gp.json", import.meta.url), "utf8"));
const epoch = Date.parse(makeSchedule(gp, new Date(Date.now()), { maxTleAgeDays: 1e9 }).tleEpoch);
const sched = makeSchedule(gp, new Date(epoch + 3600e3));   // one hour after the orbit data's epoch: always the same file
const schedFile = join(dir, "passes.json");
writeFileSync(schedFile, JSON.stringify(sched));

let status = run(process.env.CXX || "c++", [...flags, "-o", exe, "tests/firmware/rqs_test.cpp"]);
if (status === 0) status = run(exe, [...process.argv.slice(2), "--schedule", schedFile, String(sched.passes.length)]);
rmSync(dir, { recursive: true, force: true });
process.exit(status);
