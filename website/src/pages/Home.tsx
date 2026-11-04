import { HeroDrawing } from "../components/HeroDrawing";
import { site } from "../site.config";

const pic = (name: string) => `/pictures/user-guide/images/${name}`;

// What works today, each with a real picture of the app.
const TODAY = [
  {
    title: "Sketch it, then give it real sizes",
    text: "Draw lines, circles, arcs and splines on a flat sketch, then pin them down with constraints like parallel and tangent, and dimensions in mm or inches. Anything still free to move shows in blue.",
    picture: pic("app-sketch-edit.png"),
    alt: "A sketch in SketchBillet with its dimensions and constraints",
    link: { href: "/guide/sketches/", text: "How sketches work" },
  },
  {
    title: "Change one number, and the part follows",
    text: "Name the sizes that matter, like wall = 3 mm, and work the rest out from them: height = wall * 4 + 1 in. Units are checked, so a length never gets added to a plain number.",
    picture: pic("app-parameters.png"),
    alt: "The Parameters panel listing wall, width and height, beside a box whose size uses them",
    link: { href: "/guide/parameters/", text: "Parameters" },
  },
  {
    title: "Drag it where it goes",
    text: "Pick a body and drag its arrows to slide it along x, y or z, or across the ground. Every step can be undone, and every step is saved in the design so you can change it later.",
    picture: pic("app-drag-handles.png"),
    alt: "A keychain tag picked in the 3D view, with red, green and blue arrows to drag it",
    link: { href: "/guide/moving/", text: "Moving bodies" },
  },
  {
    title: "Know what it weighs and costs",
    text: "Choose what each body is made of, from PLA and PETG to aluminum and oak, and see its mass, weight and material cost. Then export STL for your slicer or STEP for other CAD programs.",
    picture: pic("app-material.png"),
    alt: "Properties showing a body's material, mass and cost",
    link: { href: "/guide/materials/", text: "Materials" },
  },
];

const LEARN = [
  { href: "/tutorials/", title: "Tutorials", text: "Make a keychain tag, a desk organizer and a name tag, step by step." },
  { href: "/guide/", title: "User's guide", text: "Every feature explained, with pictures, for someone new to CAD." },
  { href: "/cli/", title: "Command line", text: "sketchbillet_cli builds and checks designs from a script. Every example is tested." },
  { href: "/plugins/", title: "Plugin guide", text: "SketchBillet's own features are plugins. Write one in about 30 minutes." },
];

export function Home() {
  return (
    <>
      <section className="hero">
        <div className="hero-inner">
          <div className="hero-text">
            <h1>Free CAD for parts you can make.</h1>
            <p className="lede">
              SketchBillet is a 3D design program for classrooms, makers and small shops. Sketch a shape, give it
              real sizes, and turn it into a part you can 3D print or machine. It runs on Windows, macOS and Linux,
              works offline, and is free for good.
            </p>
            <div className="hero-actions">
              <a className="button primary" href="/tutorials/01-keychain-tag/">
                Make your first part
              </a>
              <a className="button" href="/guide/">
                Read the user's guide
              </a>
            </div>
          </div>
          <HeroDrawing />
        </div>
      </section>

      <section className="band" aria-labelledby="today">
        <div className="band-inner">
          <h2 id="today">What it does today</h2>
          <p className="band-lede">
            SketchBillet is early: it's being built in the open, a step at a time, and these pictures are the real
            app as it is now.
          </p>
          <div className="features">
            {TODAY.map((f) => (
              <article className="feature" key={f.title}>
                <figure>
                  <img src={f.picture} alt={f.alt} loading="lazy" width="1280" height="800" />
                </figure>
                <div className="feature-text">
                  <h3>{f.title}</h3>
                  <p>{f.text}</p>
                  <a href={f.link.href}>{f.link.text}</a>
                </div>
              </article>
            ))}
          </div>
        </div>
      </section>

      <section className="band band-plain" aria-labelledby="free">
        <div className="band-inner two-col">
          <div>
            <h2 id="free">Free, and it stays free</h2>
            <p className="band-lede">
              Every feature, up to the kind professional engineers use, is free and open source. No paid edition, no
              locked features, no trial that runs out.
            </p>
          </div>
          <ul className="promises">
            <li>
              <strong>No ads and no selling your data.</strong> Your designs stay on your computer, and nothing is
              sent anywhere without asking.
            </li>
            <li>
              <strong>No account to make.</strong> Install it and start: good for school computers.
            </li>
            <li>
              <strong>Sponsors never steer it.</strong> Support pays for time, not for say over what gets built.
            </li>
            <li>
              <strong>Open file format.</strong> A design is a zip of readable JSON, so it's yours to keep and to read.
            </li>
          </ul>
        </div>
      </section>

      <section className="band" aria-labelledby="learn">
        <div className="band-inner">
          <h2 id="learn">Learn it</h2>
          <ul className="learn-list">
            {LEARN.map((l) => (
              <li key={l.href}>
                <a href={l.href}>{l.title}</a>
                <p>{l.text}</p>
              </li>
            ))}
          </ul>
        </div>
      </section>

      <section className="band band-plain" aria-labelledby="get">
        <div className="band-inner two-col">
          <div>
            <h2 id="get">Get SketchBillet</h2>
            <p className="band-lede">
              There's no installer yet. Installers for Windows, macOS and Linux come with the first release, v0.1.
              Until then, you can build it yourself from the source code.
            </p>
          </div>
          <div className="get-steps">
            <p>
              The <a href={`${site.repo}#readme`}>README on GitHub</a> lists what to install and the three commands
              that build it. On Windows, follow the <a href="/design/windows-setup/">Windows setup guide</a>.
            </p>
            <p>
              Want to know when v0.1 is out, or help get it there? See <a href="/follow/">Follow and support</a>, or
              say hello in <a href={site.discussions}>GitHub Discussions</a>.
            </p>
          </div>
        </div>
      </section>
    </>
  );
}
