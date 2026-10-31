// Tiny static server for local preview and tests. No dependencies.
//   node tests/serve.mjs                 site/ with the live Firebase source
//   node tests/serve.mjs --fixtures      site/ with js/source.js swapped for the labeled test fixture source
//   --port 4174                          choose a port (default 4173)
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize, resolve } from "node:path";

const args = process.argv.slice(2);
const fixtures = args.includes("--fixtures");
const port = Number(args[args.indexOf("--port") + 1]) || Number(process.env.PORT) || 4173;
const ROOT = resolve(new URL("../site", import.meta.url).pathname);
const FIXTURE_SOURCE = resolve(new URL("./fixtures/fixture-source.js", import.meta.url).pathname);

const TYPES = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon",
};

createServer(async (req, res) => {
  const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
  let file = normalize(join(ROOT, path.endsWith("/") ? path + "index.html" : path));
  if (!file.startsWith(ROOT)) { res.writeHead(403).end(); return; }
  if (fixtures && path === "/js/source.js") file = FIXTURE_SOURCE;
  try {
    const body = await readFile(file);
    res.writeHead(200, { "Content-Type": TYPES[extname(file)] || "application/octet-stream", "Cache-Control": "no-store" });
    res.end(body);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain" }).end("Not found");
  }
}).listen(port, () => {
  console.log(`Serving site/ on http://localhost:${port}${fixtures ? "  (TEST FIXTURE data source)" : ""}`);
});
