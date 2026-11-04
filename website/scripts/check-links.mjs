// Checks the built site: every link to another page of the site goes to a page
// that exists, every #part of a page is there, and every picture is there. Fails
// (exit code 1) and lists each broken one, so a broken link never gets published.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const site = path.resolve(here, "..", "dist", "site");

function htmlFiles(dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = path.join(dir, e.name);
    return e.isDirectory() ? htmlFiles(p) : e.name.endsWith(".html") ? [p] : [];
  });
}

const files = htmlFiles(site);
const ids = new Map(); // route -> ids on that page
const routeOf = (file) => "/" + path.relative(site, file).split(path.sep).join("/").replace(/index\.html$/, "");
for (const f of files) {
  const html = fs.readFileSync(f, "utf8");
  ids.set(routeOf(f), new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1])));
}

const broken = [];
for (const f of files) {
  if (f.endsWith("404.html")) continue;
  const html = fs.readFileSync(f, "utf8").replace(/<script>window\.__PAGE__[\s\S]*?<\/script>/, "");
  const from = routeOf(f);
  for (const [, attr, raw] of html.matchAll(/\s(href|src)="([^"]+)"/g)) {
    const url = raw.replace(/&amp;/g, "&");
    if (/^(https?:|mailto:|data:)/.test(url)) continue;
    const [p, hash] = url.split("#");
    const route = p === "" ? from : p;
    if (!route.startsWith("/")) {
      broken.push(`${from}: ${attr}="${url}" isn't an address from the site's top (start it with /)`);
      continue;
    }
    if (route.startsWith("/assets/") || /\.[a-z0-9]+$/i.test(route)) {
      if (!fs.existsSync(path.join(site, route))) broken.push(`${from}: ${url} isn't in the site`);
      continue;
    }
    const page = route.endsWith("/") ? route : route + "/";
    if (!ids.has(page)) {
      broken.push(`${from}: links to ${url}, but there's no page ${page}`);
    } else if (hash && !ids.get(page).has(decodeURIComponent(hash))) {
      broken.push(`${from}: links to ${url}, but ${page} has no part called #${hash}`);
    }
  }
}

if (broken.length) {
  console.error(`${broken.length} broken link(s):\n  ` + broken.join("\n  "));
  process.exit(1);
}
console.log(`${files.length} pages, every link and picture is there`);
