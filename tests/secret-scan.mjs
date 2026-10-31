// Fails if anything that looks like a credential, or the old project name, is in the repo.
// Usage: node tests/secret-scan.mjs            (all tracked and untracked, non-ignored files)
//        node tests/secret-scan.mjs --staged   (only files staged for commit; used by the pre-commit hook)
import { execSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";
import { basename } from "node:path";

const staged = process.argv.includes("--staged");
const list = staged
  ? execSync("git diff --cached --name-only --diff-filter=ACMR", { encoding: "utf8" })
  : execSync("git ls-files -co --exclude-standard", { encoding: "utf8" });
const files = list.split("\n").filter(Boolean);

// Third-party skill docs contain sample passwords such as "pass123"; they are not ours to scan.
const SKIP = [/^\.claude\/skills\//, /^node_modules\//, /package-lock\.json$/, /\.(png|jpg|jpeg|gif|ico|woff2?|ttf)$/i];

// Placeholder values from secrets.example.h are allowed.
const PLACEHOLDER = /^(your-|a-long-random-password|esp32@example\.com)/;

const OLD_NAME = ["ar", "gos"].join(""); // built at runtime so this file does not trip its own check

const RULES = [
  { name: "Google API key", re: /AIza[0-9A-Za-z_\-]{35}/ },
  { name: "private key block", re: /-----BEGIN [A-Z ]*PRIVATE KEY-----/ },
  { name: "JWT or ID token", re: /eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}/ },
  { name: "ThingSpeak-style key in a URL", re: /api_key=[A-Z0-9]{16}/i },
  {
    name: "firmware secret define",
    re: /#define\s+(WIFI_PASS|WIFI_SSID|FIREBASE_API_KEY|FIREBASE_USER_PASSWORD|FIREBASE_USER_EMAIL)\s+"([^"]*)"/,
    allow: (m) => PLACEHOLDER.test(m[2]),
  },
  {
    name: "password or key assignment",
    re: /\b(password|passwd|secret|api[_-]?key|write[_-]?key)\b["']?\s*[:=]\s*["']([^"'\s]{8,})["']/i,
    allow: (m) => PLACEHOLDER.test(m[2]),
  },
  { name: "old project name", re: new RegExp(OLD_NAME, "i") },
];

const problems = [];
for (const f of files) {
  if (SKIP.some((re) => re.test(f))) continue;
  if (basename(f) === "secrets.h") { problems.push(`${f}: secrets.h must never be committed`); continue; }
  if (new RegExp(OLD_NAME, "i").test(f)) problems.push(`${f}: file name contains the old project name`);
  if (!existsSync(f)) continue;
  let text;
  try { text = readFileSync(f, "utf8"); } catch { continue; }
  text.split("\n").forEach((line, i) => {
    for (const rule of RULES) {
      const m = line.match(rule.re);
      if (m && !(rule.allow && rule.allow(m))) problems.push(`${f}:${i + 1}: ${rule.name}`);
    }
  });
}

if (problems.length) {
  console.error("Secret scan FAILED:\n  " + problems.join("\n  "));
  process.exit(1);
}
console.log(`Secret scan passed (${files.length} files checked).`);
