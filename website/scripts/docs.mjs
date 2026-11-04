// Turns the repository's docs/ folder into the website's guide pages, at build
// time: Markdown to HTML, links between pages rewritten to the site's addresses,
// pictures copied next to the site, and links to code pointed at GitHub. The
// guides stay the one source: the site is made from them every build.

import fs from "node:fs";
import path from "node:path";
import MarkdownIt from "markdown-it";
import GithubSlugger from "github-slugger";

export const REPO_URL = "https://github.com/ddreeves70/sketchbillet";

// The parts of docs/ the site shows, in the order of the menu. `index` is the
// page a section opens on; its links set the order of the section's pages.
export const SECTIONS = [
  { id: "guide", title: "User's guide", dir: "user-guide", base: "/guide/", index: "README.md" },
  { id: "tutorials", title: "Tutorials", dir: "tutorials", base: "/tutorials/", index: "README.md" },
  { id: "cli", title: "Command line", dir: "cli", base: "/cli/", index: "README.md" },
  { id: "plugins", title: "Plugin guide", dir: "plugin-guide", base: "/plugins/", index: "README.md" },
  { id: "design", title: "Design", dir: "design", base: "/design/", index: "README.md",
    // Pages kept at the top of docs/ that belong with the design notes.
    extra: ["../file-format.md", "../dependencies.md", "../windows-setup.md"] },
];

// A page's address on the site, from its file (relative to docs/).
function routeOf(docsRel) {
  const norm = docsRel.split(path.sep).join("/");
  for (const s of SECTIONS) {
    const inDir = norm.startsWith(s.dir + "/") && !norm.slice(s.dir.length + 1).includes("/");
    const extra = (s.extra || []).map((e) => path.posix.normalize(path.posix.join(s.dir, e)));
    if (inDir || extra.includes(norm)) {
      const name = path.posix.basename(norm, ".md");
      return name === "README" ? s.base : `${s.base}${name}/`;
    }
  }
  return null;
}

function firstHeading(markdown) {
  const m = markdown.match(/^#\s+(.+)$/m);
  return m ? m[1].replace(/`/g, "").trim() : null;
}

// Reads every section's pages, in menu order.
export function loadDocs(repoRoot, outDir) {
  const docsRoot = path.join(repoRoot, "docs");
  const pictures = new Map(); // docs-relative picture -> site path
  const pages = [];
  const sections = [];

  for (const s of SECTIONS) {
    const dir = path.join(docsRoot, s.dir);
    const files = fs.readdirSync(dir).filter((f) => f.endsWith(".md"));
    // The order: the index first, then pages in the order the index links them, then the rest.
    const indexText = fs.readFileSync(path.join(dir, s.index), "utf8");
    const linked = [...indexText.matchAll(/\]\(([^)#\s]+\.md)/g)].map((m) => path.posix.normalize(m[1]));
    const order = [s.index];
    for (const l of linked) if (files.includes(l) && !order.includes(l)) order.push(l);
    for (const e of s.extra || []) order.push(e);
    for (const f of files.sort()) if (!order.includes(f)) order.push(f);

    const sectionPages = [];
    for (const rel of order) {
      const file = path.join(dir, rel);
      const docsRel = path.relative(docsRoot, file);
      const markdown = fs.readFileSync(file, "utf8");
      const route = routeOf(docsRel);
      const title = firstHeading(markdown) || path.basename(rel, ".md");
      const page = { section: s.id, route, title, file: docsRel.split(path.sep).join("/"), markdown };
      sectionPages.push(page);
      pages.push(page);
    }
    sections.push({ id: s.id, title: s.title, base: s.base, pages: sectionPages.map((p) => ({ title: p.title, route: p.route })) });
  }

  for (const page of pages) page.html = render(page, docsRoot, pictures);
  // Pictures the pages use, copied to the site.
  for (const [docsRel, sitePath] of pictures) {
    const from = path.join(docsRoot, docsRel);
    const to = path.join(outDir, sitePath);
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.copyFileSync(from, to);
  }
  for (const page of pages) delete page.markdown;
  return { sections, pages };
}

function render(page, docsRoot, pictures) {
  const md = new MarkdownIt({ html: true, linkify: false, typographer: false });
  const slugger = new GithubSlugger();
  const pageDir = path.posix.dirname(page.file);
  const headings = [];

  // Heading ids as GitHub makes them, so links like commands.md#featureadd_box work.
  md.core.ruler.push("heading_ids", (state) => {
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length; i++) {
      if (tokens[i].type !== "heading_open") continue;
      const inline = tokens[i + 1];
      const text = inline.children.filter((t) => t.type === "text" || t.type === "code_inline").map((t) => t.content).join("");
      const id = slugger.slug(text);
      tokens[i].attrSet("id", id);
      if (tokens[i].tag === "h2") headings.push({ id, text });
    }
  });

  // Where a link in the Markdown goes on the site.
  const target = (href) => {
    if (/^(https?:|mailto:|#)/.test(href)) return href;
    const [p, hash] = href.split("#");
    const resolved = path.posix.normalize(path.posix.join(pageDir, p));
    const anchor = hash ? `#${hash}` : "";
    if (resolved.endsWith(".md")) {
      const route = routeOf(resolved);
      if (route) return route + anchor;
      // A Markdown file outside the guides (the README, CLAUDE.md): on GitHub.
      return `${REPO_URL}/blob/main/${path.posix.normalize(path.posix.join("docs", resolved))}${anchor}`;
    }
    // A file or folder in the repository (code, a CSV): on GitHub.
    const repoPath = path.posix.normalize(path.posix.join("docs", resolved));
    const isDir = p.endsWith("/") || (fs.existsSync(path.join(docsRoot, resolved)) && fs.statSync(path.join(docsRoot, resolved)).isDirectory());
    return `${REPO_URL}/${isDir ? "tree" : "blob"}/main/${repoPath.replace(/\/$/, "")}${anchor}`;
  };

  const defaultLink = md.renderer.rules.link_open || ((tokens, idx, options, env, self) => self.renderToken(tokens, idx, options));
  md.renderer.rules.link_open = (tokens, idx, options, env, self) => {
    const href = tokens[idx].attrGet("href");
    if (href) {
      const to = target(href);
      tokens[idx].attrSet("href", to);
      if (/^https?:/.test(to)) tokens[idx].attrSet("rel", "noopener");
    }
    return defaultLink(tokens, idx, options, env, self);
  };
  md.renderer.rules.image = (tokens, idx) => {
    const t = tokens[idx];
    const src = t.attrGet("src");
    const alt = t.children ? t.children.map((c) => c.content).join("") : t.content;
    let to = src;
    if (src && !/^https?:/.test(src)) {
      const docsRel = path.posix.normalize(path.posix.join(pageDir, src));
      to = `/pictures/${docsRel}`;
      pictures.set(docsRel, to);
    }
    const esc = (s) => s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
    return `<img src="${esc(to)}" alt="${esc(alt)}" loading="lazy">`;
  };
  // Wide tables scroll sideways on a phone instead of pushing the page wider.
  md.renderer.rules.table_open = () => '<div class="table-wrap"><table>';
  md.renderer.rules.table_close = () => "</table></div>";

  // The first heading is the page's title, shown by the page itself.
  const body = page.markdown.replace(/^#\s+.+\n/, "");
  const html = md.render(body);
  page.headings = headings;
  return html;
}
