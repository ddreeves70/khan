// What each page is given to show. The build works it out for every address
// and puts it in the page, so the browser shows exactly what was built.

export type NavLink = { title: string; route: string };

export type DocSection = { id: string; title: string; base: string; pages: NavLink[] };

export type DocPage = {
  section: string;
  route: string;
  title: string;
  file: string; // in docs/, for "Change this page on GitHub"
  html: string;
  headings: { id: string; text: string }[];
};

// One download on the Download page, found on GitHub by scripts/releases.mjs.
export type Installer = { os: "windows" | "macos" | "linux"; arch: string; name: string; url: string; size: number };

export type Release = {
  tag: string;
  version: string; // "0.1.0", or "preview"
  date: string; // when it was published, ISO 8601
  notes: string;
  url: string; // its page on GitHub
  installers: Installer[];
};

// checked is false when the build couldn't reach GitHub.
export type Releases = { checked: boolean; latest: Release | null; preview: Release | null };

export type PageData =
  | { kind: "home"; route: "/"; title: string; description: string }
  | { kind: "story" | "people" | "follow"; route: string; title: string; description: string }
  | {
      kind: "doc";
      route: string;
      title: string;
      description: string;
      doc: DocPage;
      section: DocSection;
      prev: NavLink | null;
      next: NavLink | null;
    }
  | { kind: "download"; route: string; title: string; description: string; releases: Releases }
  | { kind: "notfound"; route: string; title: string; description: string };

declare global {
  interface Window {
    __PAGE__: PageData;
  }
}
