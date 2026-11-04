// Everything about the site that changes without changing its code: where
// SketchBillet is on other sites, and how to write to it. Leave a link empty
// ("") until it exists; the site only shows the ones that are filled in.

export const site = {
  name: "SketchBillet",
  url: "https://sketchbillet.org",
  repo: "https://github.com/ddreeves70/sketchbillet",
  discussions: "https://github.com/ddreeves70/sketchbillet/discussions",

  // Where SketchBillet posts videos and news.
  social: {
    youtube: "", // like "https://www.youtube.com/@sketchbillet"
    tiktok: "", // like "https://www.tiktok.com/@sketchbillet"
    instagram: "", // like "https://www.instagram.com/sketchbillet"
    x: "", // like "https://x.com/sketchbillet"
  },

  // Supporting the project with money.
  patreon: "", // like "https://www.patreon.com/sketchbillet"

  // Email addresses, shown on the Follow and support page. Leave empty until the mailbox works.
  email: {
    hello: "", // general questions, like "hello@sketchbillet.org"
    schools: "", // teachers and schools, like "schools@sketchbillet.org"
    security: "", // reporting a security problem, like "security@sketchbillet.org"
  },

  // The people who make SketchBillet, for the People page. Add a line for each.
  people: [
    {
      name: "ddreeves70",
      role: "Started SketchBillet, and writes and designs it",
      github: "https://github.com/ddreeves70",
      about: "", // a few sentences about you, in your own words
    },
  ],
};

export type Site = typeof site;
