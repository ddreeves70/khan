import { useEffect, useState } from "react";
import { site } from "../site.config";
import type { Installer, Release, Releases } from "../types";

type Os = Installer["os"];

const SYSTEMS: { os: Os; name: string; needs: string; kind: string; steps: string }[] = [
  {
    os: "windows",
    name: "Windows",
    needs: "Windows 10 or 11, 64-bit",
    kind: ".msi installer",
    steps: "Open it. If Windows says it protected your PC, click More info, then Run anyway.",
  },
  {
    os: "macos",
    name: "macOS",
    needs: "macOS 15 or newer, on a Mac with Apple silicon (M1 or newer)",
    kind: ".dmg disk image",
    steps:
      "Open it and drag SketchBillet to Applications. The first time, allow it in System Settings > Privacy & Security > Open Anyway.",
  },
  {
    os: "linux",
    name: "Linux",
    needs: "64-bit, from 2024 or newer, like Ubuntu 24.04 or Fedora 40",
    kind: "AppImage, the whole program in one file",
    steps: "Allow it to run as a program (chmod +x), then double-click it.",
  },
];

const releasesPage = `${site.repo}/releases`;

function size(bytes: number) {
  return `${Math.round(bytes / 1e6)} MB`;
}

// Written the same way when the page is built and in the browser, so the two match.
function day(iso: string) {
  return new Date(iso).toLocaleDateString("en-US", { month: "short", day: "numeric", year: "numeric", timeZone: "UTC" });
}

// The visitor's system, worked out in the browser after the page loads.
function useVisitorOs(): Os | null {
  const [os, setOs] = useState<Os | null>(null);
  useEffect(() => {
    const ua = navigator.userAgent;
    if (/Windows/.test(ua)) setOs("windows");
    else if (/Macintosh|Mac OS X/.test(ua) && !/iPhone|iPad/.test(ua)) setOs("macos");
    else if (/Linux/.test(ua) && !/Android/.test(ua)) setOs("linux");
  }, []);
  return os;
}

function Downloads({ release, primary, yours }: { release: Release; primary: boolean; yours: Os | null }) {
  return (
    <ul className="downloads">
      {SYSTEMS.map((s) => {
        const file = release.installers.find((i) => i.os === s.os);
        return (
          <li key={s.os} className={yours === s.os ? "download yours" : "download"}>
            <div className="download-what">
              <h3>
                {s.name}
                {yours === s.os && <span className="download-yours">Your computer</span>}
              </h3>
              <p>{s.needs}</p>
            </div>
            {file ? (
              <div className="download-get">
                <a className={primary ? "button primary" : "button"} href={file.url}>
                  Download for {s.name}
                </a>
                <p>
                  {s.kind}, {size(file.size)}
                </p>
              </div>
            ) : (
              <div className="download-get">
                <p>Not in this {release.version === "preview" ? "build" : "release"} yet.</p>
              </div>
            )}
            <p className="download-steps">{s.steps}</p>
          </li>
        );
      })}
    </ul>
  );
}

export function Download({ releases }: { releases: Releases }) {
  const yours = useVisitorOs();
  const { latest, preview } = releases;
  return (
    <article className="page-prose page-download">
      <header className="page-head">
        <h1>Download SketchBillet</h1>
        <p className="lede">
          For Windows, macOS and Linux. Free, with no account, and it works without the internet once it's installed.
        </p>
      </header>

      {latest && (
        <section aria-labelledby="release">
          <h2 id="release">SketchBillet {latest.version}</h2>
          <p className="release-meta">
            Released {day(latest.date)}. <a href={latest.url}>What's new in {latest.version}</a>
          </p>
          <Downloads release={latest} primary yours={yours} />
        </section>
      )}

      {preview && (
        <section aria-labelledby="preview">
          <h2 id="preview">Preview build</h2>
          <p>
            {latest
              ? "The newest SketchBillet, rebuilt every time a change is added. It has features the release doesn't, and maybe new bugs. For lessons and real work, use the release above."
              : "There's no release yet: SketchBillet 0.1 is on its way. Until then, this is the newest SketchBillet, rebuilt every time a change is added. It's early, so expect rough edges, and tell us what's hard."}
          </p>
          <p className="release-meta">
            Built {day(preview.date)}. <a href={preview.url}>What's in it</a>
          </p>
          <Downloads release={preview} primary={!latest} yours={yours} />
        </section>
      )}

      {!latest && !preview && (
        <section aria-labelledby="none">
          <h2 id="none">Downloads</h2>
          <p>
            {releases.checked
              ? "The first downloads are being made right now. Check back soon, or see "
              : "Every download is on "}
            <a href={releasesPage}>SketchBillet's releases on GitHub</a>.
          </p>
        </section>
      )}

      <section aria-labelledby="help">
        <h2 id="help">Help installing</h2>
        <p>
          <a href="/guide/install/">Installing SketchBillet</a> goes through each system step by step, with what to
          click when your computer warns you about a new program. Schools can install the Windows .msi on many computers
          at once, with no questions: <code>msiexec /i SketchBillet.msi /qn</code>.
        </p>
        <p>
          Every download includes <a href="/cli/">sketchbillet_cli</a>, the command-line tool, as well as the app.
          Older versions are on <a href={releasesPage}>GitHub</a>. To build SketchBillet from its source code instead,
          follow the <a href={`${site.repo}#readme`}>README</a>.
        </p>
      </section>
    </article>
  );
}
