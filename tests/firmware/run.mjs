// Builds and runs the firmware logic tests on this computer (no ESP32 needed).
// Usage: npm run test:firmware [-- -v]
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const dir = mkdtempSync(join(tmpdir(), "soh-test-"));
const exe = join(dir, "soh_test");
const flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wshadow", "-Werror"];
const run = (cmd, args) => spawnSync(cmd, args, { stdio: "inherit" }).status ?? 1;

let status = run(process.env.CXX || "c++", [...flags, "-o", exe, "tests/firmware/soh_test.cpp"]);
if (status === 0) status = run(exe, process.argv.slice(2));
rmSync(dir, { recursive: true, force: true });
process.exit(status);
