import { site } from "../site.config";

// What has happened so far, oldest first. A real sequence, so it's numbered by date.
const SO_FAR = [
  {
    date: "Oct 21, 2026",
    title: "The first code",
    text: "The project starts: the build, the tests, and automatic builds on Windows, macOS and Linux from day one.",
  },
  {
    date: "Oct 23",
    title: "Two decisions",
    text: "Capability comes first: the shortest path to a modeler that really works. And the guides grow with the code, with every example run as a test.",
  },
  {
    date: "Oct 27",
    title: "Foundations finished",
    text: "Make a box, undo, save, open it again, and export it for 3D printing, checked on all three systems. The box tool and the 3D viewer are already plugins.",
  },
  {
    date: "Oct 28 to 31",
    title: "The sketcher",
    text: "Lines, circles, arcs and splines, constraints and dimensions, colors that show what can still move, and extruding a sketch into a solid.",
  },
  {
    date: "Oct 31",
    title: "Parametric modeling begins",
    text: "Parameters: named sizes with units and formulas, which sketch dimensions and steps can use. And dragging bodies with arrows in the 3D view.",
  },
  {
    date: "Nov 4",
    title: "SketchBillet.org",
    text: "This website, made from the same guides that ship with the code.",
  },
];

// What comes next, in order. From the roadmap; there are no dates, because the pace depends on volunteer time.
const NEXT = [
  { title: "The parametric core", text: "Revolve, fillets, chamfers, holes, shells, mirrors and patterns, and a timeline you can roll back." },
  { title: "v0.1, the first release", text: "Installers for Windows, macOS and Linux, 3MF export and checks that a part will print." },
  { title: "v0.2, assemblies and drawings", text: "Parts that fit together and move, and drawings with dimensions you can print or send as PDF." },
  { title: "Python and the community", text: "Scripts, custom features and a plugin manager, so anyone can add to SketchBillet." },
  { title: "Makers and hobby CNC", text: "Working with meshes, and toolpaths for CNC routers and mills." },
  { title: "Small engineering shops", text: "Sheet metal, tolerances on drawings, and checking a part's strength." },
];

export function Story() {
  return (
    <article className="page-prose">
      <header className="page-head">
        <h1>Story</h1>
        <p className="lede">
          Why SketchBillet exists, where it's got to, and where it's going.
        </p>
      </header>

      <section aria-labelledby="why">
        <h2 id="why">Why</h2>
        <p>
          Good CAD software is how people learn to design things that get made: brackets, enclosures, knobs, parts
          for robots. The programs most schools and makers use are owned by companies that can change the price, the
          license or the rules whenever they like, and many need an account and an internet connection to start.
        </p>
        <p>
          SketchBillet is the other way round. It's free and open source, runs on the computer in front of you, and
          keeps your designs there. It starts with what a high school engineering class needs, then grows to makers,
          people with CNC machines, and small shops. The long-term goal is a tool as capable as SolidWorks that is
          free forever.
        </p>
        <p>
          A billet is a solid block of metal or plastic that a part is machined from. A sketch is where every part
          starts. SketchBillet is the journey from one to the other.
        </p>
      </section>

      <section aria-labelledby="how">
        <h2 id="how">How it's built</h2>
        <p>
          SketchBillet stands on proven open-source work: the Open CASCADE geometry kernel, which also powers
          FreeCAD; FreeCAD's PlaneGCS sketch solver; and Qt for the windows and panels. What's new is the part that
          makes it feel simple: the design model, the commands that make undo always work, and messages written for
          a 14-year-old, with units on every number and a way to fix the problem.
        </p>
        <p>
          Everything SketchBillet does is a plugin, its own features included, so the API anyone can use is the one
          SketchBillet uses itself. Every pull request updates the guides, and every example in them runs as a test,
          so what you read is what you get.
        </p>
      </section>

      <section aria-labelledby="so-far">
        <h2 id="so-far">So far</h2>
        <ol className="timeline">
          {SO_FAR.map((e) => (
            <li key={e.title}>
              <time>{e.date}</time>
              <div>
                <h3>{e.title}</h3>
                <p>{e.text}</p>
              </div>
            </li>
          ))}
        </ol>
      </section>

      <section aria-labelledby="next">
        <h2 id="next">What's next</h2>
        <p>
          SketchBillet is built in phases, each ending with a build you can try. The full plan is in the{" "}
          <a href="/design/roadmap/">roadmap</a>.
        </p>
        <ol className="phases">
          {NEXT.map((p) => (
            <li key={p.title}>
              <h3>{p.title}</h3>
              <p>{p.text}</p>
            </li>
          ))}
        </ol>
        <p>
          Ideas and questions are welcome in <a href={site.discussions}>GitHub Discussions</a>.
        </p>
      </section>
    </article>
  );
}
