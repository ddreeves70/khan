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
  | { kind: "notfound"; route: string; title: string; description: string };

declare global {
  interface Window {
    __PAGE__: PageData;
  }
}
