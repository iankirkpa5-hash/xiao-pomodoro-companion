const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const port = Number(process.argv[2] || 8765);
if (!Number.isInteger(port) || port < 1024 || port > 65535) {
  throw new Error("Choose a port between 1024 and 65535.");
}
const files = {
  "/": ["index.html", "text/html; charset=utf-8"],
  "/index.html": ["index.html", "text/html; charset=utf-8"],
  "/styles.css": ["styles.css", "text/css; charset=utf-8"],
  "/monitor.mjs": ["monitor.mjs", "text/javascript; charset=utf-8"],
  "/telemetry.mjs": ["telemetry.mjs", "text/javascript; charset=utf-8"],
};
const server = http.createServer((req, res) => {
  const pathname = new URL(req.url, "http://127.0.0.1").pathname;
  if (pathname === "/favicon.ico") { res.writeHead(204); return res.end(); }
  const file = Object.hasOwn(files, pathname) ? files[pathname] : null;
  if (!file || !["GET", "HEAD"].includes(req.method)) {
    res.writeHead(404); return res.end("Not found");
  }
  fs.readFile(path.join(__dirname, file[0]), (error, data) => {
    if (error) { res.writeHead(500); return res.end("Unable to read page"); }
    res.writeHead(200, {
      "Content-Type": file[1],
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
      "Permissions-Policy": "serial=(self), microphone=()",
      "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
    });
    res.end(req.method === "HEAD" ? undefined : data);
  });
});
server.on("error", error => { console.error(error.message); process.exitCode = 1; });
server.listen(port, "127.0.0.1", () => console.log(`XIAO microphone monitor: http://127.0.0.1:${port}`));
