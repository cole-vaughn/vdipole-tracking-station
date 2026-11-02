// Rebuilds the self-hosted libraries in site/vendor/ from the exact versions pinned in package.json.
// The site itself has no build step: these files are committed. Run after changing a library version:
//   npm run vendor
import { build } from "esbuild";

const common = { bundle: true, format: "esm", minify: true, target: "es2020", legalComments: "linked", logLevel: "info" };
await build({ ...common, entryPoints: ["tools/vendor/scene-entry.js"], outfile: "site/vendor/scene.lib.js" });
// satellite.js also ships optional WebAssembly runtimes that mention Node.js modules; the site never loads them,
// so they are marked external and tree-shaking drops them (checked below).
await build({
  ...common, target: "es2022", external: ["node:*"],
  entryPoints: ["tools/vendor/satellite-entry.js"], outfile: "site/vendor/satellite.lib.js",
});
const { readFileSync } = await import("node:fs");
if (/node:|wasm/.test(readFileSync("site/vendor/satellite.lib.js", "utf8"))) throw new Error("satellite.lib.js still references Node or WASM code");
