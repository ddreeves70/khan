// Finds SketchBillet's installers on GitHub, for the Download page: the newest
// release (from a version tag, like v0.1.0) and the preview build (from main).
// CI's "publish" job puts them there and then rebuilds the site.
//
// Without the internet the build still works: the page then links to GitHub's
// releases page instead. Set GH_TOKEN (CI does) to avoid GitHub's limit on
// how often anyone may ask.

const API = "https://api.github.com/repos/ddreeves70/sketchbillet/releases?per_page=30";

// SketchBillet-0.1.0-windows-x64.msi, SketchBillet-preview-macos-arm64.dmg, ...
const NAME = /^SketchBillet-[^-]+-(windows|macos|linux)-(x64|arm64)\.(msi|dmg|AppImage)$/;

function installers(release) {
  return release.assets
    .map((a) => {
      const m = a.name.match(NAME);
      return m && { os: m[1], arch: m[2], name: a.name, url: a.browser_download_url, size: a.size };
    })
    .filter(Boolean)
    .sort((a, b) => ["windows", "macos", "linux"].indexOf(a.os) - ["windows", "macos", "linux"].indexOf(b.os));
}

function summary(release, preview) {
  return {
    tag: release.tag_name,
    version: preview ? "preview" : release.tag_name.replace(/^v/, ""),
    date: release.published_at || release.created_at,
    // The preview's notes say which commit it was built from.
    notes: preview ? (release.body || "").trim() : "",
    url: release.html_url,
    installers: installers(release),
  };
}

export async function loadReleases() {
  // To try the page with made-up releases: SB_RELEASES_FILE=my-releases.json npm run build
  // (the file is what this function returns).
  if (process.env.SB_RELEASES_FILE) {
    const fs = await import("node:fs");
    return JSON.parse(fs.readFileSync(process.env.SB_RELEASES_FILE, "utf8"));
  }
  const headers = { Accept: "application/vnd.github+json", "User-Agent": "sketchbillet-website-build" };
  if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
  try {
    const res = await fetch(API, { headers, signal: AbortSignal.timeout(15000) });
    if (!res.ok) throw new Error(`GitHub answered ${res.status}`);
    const all = await res.json();
    const latest = all.find((r) => !r.draft && !r.prerelease && /^v\d/.test(r.tag_name));
    const preview = all.find((r) => r.tag_name === "preview" && !r.draft);
    const found = {
      checked: true,
      latest: latest ? summary(latest, false) : null,
      preview: preview ? summary(preview, true) : null,
    };
    console.log(
      `releases: latest ${found.latest ? found.latest.version : "none"}, preview ${found.preview ? "yes" : "none"}`,
    );
    return found;
  } catch (e) {
    console.warn(`releases: couldn't ask GitHub (${e.message}); the Download page links to GitHub instead`);
    return { checked: false, latest: null, preview: null };
  }
}
