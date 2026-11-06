import { site } from "../site.config";

export function People() {
  return (
    <article className="page-prose">
      <header className="page-head">
        <h1>People</h1>
        <p className="lede">The people who make SketchBillet, and how to join them.</p>
      </header>

      <ul className="people">
        {site.people.map((p) => (
          <li key={p.name}>
            <h2>{p.name}</h2>
            <p className="role">{p.role}</p>
            {p.about && <p>{p.about}</p>}
            {(p.github || p.email) && (
              <p className="person-links">
                {p.github && (
                  <a href={p.github} rel="noopener">
                    {p.name} on GitHub
                  </a>
                )}
                {p.email && <a href={`mailto:${p.email}`}>{p.email}</a>}
              </p>
            )}
          </li>
        ))}
      </ul>

      <section aria-labelledby="join">
        <h2 id="join">Help make it</h2>
        <p>
          SketchBillet welcomes help of every kind: trying it and saying what's confusing, fixing a typo in the
          guides, testing on your computer, writing a plugin, or writing code. Teachers who try it with a class are
          especially welcome.
        </p>
        <ul className="plain-list">
          <li>
            Say hello, or ask anything, in <a href={site.discussions}>GitHub Discussions</a>
            {site.email.dev && (
              <>
                , or write to <a href={`mailto:${site.email.dev}`}>{site.email.dev}</a>
              </>
            )}
            .
          </li>
          <li>
            Report a problem in <a href={`${site.repo}/issues`}>GitHub Issues</a>, with what you did and what
            SketchBillet said.
          </li>
          <li>
            Read how the code is organized in the <a href="/design/">design notes</a>, then send a pull request. Every
            commit needs a sign-off (<code>git commit -s</code>).
          </li>
          <li>
            Everyone who has sent a change is listed on{" "}
            <a href={`${site.repo}/graphs/contributors`}>GitHub's contributors page</a>.
          </li>
        </ul>
      </section>
    </article>
  );
}
