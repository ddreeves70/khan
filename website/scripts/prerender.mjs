// Builds every page of the site ahead of time: one HTML file per address, ready
// for GitHub Pages, so the site works without JavaScript and search engines see
// every word. Runs after Vite's two builds (see package.json).

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { loadDocs } from "./docs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");
const repo = path.resolve(root, "..");
const out = path.join(root, "dist", "site");
const SITE_URL = "https://sketchbillet.org";

const template = fs.readFileSync(path.join(out, "index.html"), "utf8");
const { render } = await import(pathToFileURL(path.join(root, "dist", "server", "entry-server.js")).href);
const docs = loadDocs(repo, out);

const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/"/g, "&quot;");

// A page's description: what it says, or the first sentence or two of a guide page.
function summary(html) {
  const p = html.match(/<p>([\s\S]*?)<\/p>/);
  const text = (p ? p[1] : "").replace(/<[^>]+>/g, "").replace(/\s+/g, " ").trim();
  return text.length > 180 ? text.slice(0, 177).replace(/\s+\S*$/, "") + "..." : text;
}

const pages = [
  {
    kind: "home",
    route: "/",
    title: "SketchBillet: free CAD for parts you can make",
    description:
      "SketchBillet is free, open-source 3D design software for classrooms, makers and small shops. Sketch it, give it real sizes, and 3D print or machine it.",
  },
  { kind: "story", route: "/story/", title: "Story", description: "Why SketchBillet exists, where it's got to, and where it's going." },
  { kind: "people", route: "/people/", title: "People", description: "The people who make SketchBillet, and how to join them." },
  {
    kind: "follow",
    route: "/follow/",
    title: "Follow and support",
    description: "Where SketchBillet posts, how to write to it, and how to help pay for its time.",
  },
];
for (const section of docs.sections) {
  const list = section.pages;
  for (const doc of docs.pages.filter((p) => p.section === section.id)) {
    const i = list.findIndex((p) => p.route === doc.route);
    pages.push({
      kind: "doc",
      route: doc.route,
      title: doc.title,
      description: summary(doc.html) || `${doc.title}, from SketchBillet's ${section.title.toLowerCase()}.`,
      doc,
      section,
      prev: i > 0 ? list[i - 1] : null,
      next: i >= 0 && i < list.length - 1 ? list[i + 1] : null,
    });
  }
}
const notFound = { kind: "notfound", route: "/404/", title: "There's no page here", description: "This page doesn't exist." };

function write(page, file) {
  const title = page.kind === "home" ? page.title : `${page.title} | SketchBillet`;
  const head = [
    `<title>${esc(title)}</title>`,
    `<meta name="description" content="${esc(page.description)}">`,
    page.kind === "notfound" ? `<meta name="robots" content="noindex">` : `<link rel="canonical" href="${SITE_URL}${page.route}">`,
    `<meta property="og:title" content="${esc(title)}">`,
    `<meta property="og:description" content="${esc(page.description)}">`,
    `<meta property="og:type" content="website">`,
    `<meta property="og:image" content="${SITE_URL}/pictures/user-guide/images/app-main.png">`,
  ].join("\n    ");
  // The page's data, for the browser to pick up where the build left off.
  const data = `<script>window.__PAGE__=${JSON.stringify(page).replace(/</g, "\\u003c")}</script>`;
  const html = template
    .replace("<!--app-head-->", head)
    .replace("<!--app-html-->", render(page))
    .replace("<!--app-data-->", data);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, html);
}

for (const page of pages) write(page, path.join(out, page.route, "index.html"));
write(notFound, path.join(out, "404.html"));

fs.writeFileSync(
  path.join(out, "sitemap.xml"),
  `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${pages
    .map((p) => `  <url><loc>${SITE_URL}${p.route}</loc></url>`)
    .join("\n")}\n</urlset>\n`,
);
fs.writeFileSync(path.join(out, "robots.txt"), `User-agent: *\nAllow: /\nSitemap: ${SITE_URL}/sitemap.xml\n`);
console.log(`built ${pages.length} pages into ${path.relative(root, out)}`);
