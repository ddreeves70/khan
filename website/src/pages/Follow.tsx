import { socialLinks } from "../components/Footer";
import { site } from "../site.config";

const WHAT: Record<string, string> = {
  YouTube: "Videos: how to design a part, start to finish, and what's new.",
  TikTok: "Short tips and quick builds.",
  Instagram: "Parts people have made with SketchBillet.",
  X: "News and releases.",
  GitHub: "The code, the guides, and every change as it happens.",
};

export function Follow() {
  const channels = socialLinks().filter((l) => l.label !== "Patreon");
  const emails = [
    { label: "Questions and ideas", address: site.email.hello },
    { label: "Teachers and schools", address: site.email.schools },
    { label: "Security problems", address: site.email.security },
  ].filter((e) => e.address);
  return (
    <article className="page-prose">
      <header className="page-head">
        <h1>Follow and support</h1>
        <p className="lede">Where SketchBillet posts, how to write to it, and how to help pay for its time.</p>
      </header>

      <section aria-labelledby="follow">
        <h2 id="follow">Follow</h2>
        <ul className="channels">
          {channels.map((c) => (
            <li key={c.label}>
              <a href={c.href} rel="noopener">
                {c.label}
              </a>
              <p>{WHAT[c.label]}</p>
            </li>
          ))}
        </ul>
      </section>

      <section aria-labelledby="support">
        <h2 id="support">Support</h2>
        <p>
          SketchBillet is free and stays free. Support pays for the time to build it, and never buys a say in what
          gets built. Every feature stays free for everyone, whether you give or not.
        </p>
        {site.patreon ? (
          <p>
            <a className="button primary" href={site.patreon} rel="noopener">
              Support SketchBillet on Patreon
            </a>
          </p>
        ) : (
          <p>
            Ways to give are on their way. Until then, the biggest help is to try SketchBillet and say what was hard,
            in <a href={site.discussions}>GitHub Discussions</a>.
          </p>
        )}
      </section>

      <section aria-labelledby="write">
        <h2 id="write">Write to us</h2>
        {emails.length > 0 ? (
          <ul className="channels">
            {emails.map((e) => (
              <li key={e.label}>
                <a href={`mailto:${e.address}`}>{e.address}</a>
                <p>{e.label}</p>
              </li>
            ))}
          </ul>
        ) : (
          <p>
            Ask anything in <a href={site.discussions}>GitHub Discussions</a>, or report a problem in{" "}
            <a href={`${site.repo}/issues`}>GitHub Issues</a>.
          </p>
        )}
      </section>
    </article>
  );
}
