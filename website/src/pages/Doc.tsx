import { useState } from "react";
import { site } from "../site.config";
import type { DocPage, DocSection, NavLink } from "../types";

// A page of the guides, made from a file in docs/: the section's pages down
// the side, the page in the middle, and its headings on wide screens.
export function Doc({
  doc,
  section,
  prev,
  next,
}: {
  doc: DocPage;
  section: DocSection;
  prev: NavLink | null;
  next: NavLink | null;
}) {
  const [open, setOpen] = useState(false); // the section's pages, on a phone
  return (
    <div className="doc-layout">
      <nav className={open ? "doc-nav open" : "doc-nav"} aria-label={section.title}>
        <a className="doc-nav-title" href={section.base}>
          {section.title}
        </a>
        <button className="doc-nav-toggle" type="button" aria-expanded={open} onClick={() => setOpen(!open)}>
          {open ? "Hide the pages" : `All ${section.pages.length} pages`}
        </button>
        <ul>
          {section.pages.map((p) => (
            <li key={p.route}>
              <a href={p.route} aria-current={p.route === doc.route ? "page" : undefined}>
                {p.title}
              </a>
            </li>
          ))}
        </ul>
      </nav>
      <article className="doc">
        <h1>{doc.title}</h1>
        <div className="doc-body" dangerouslySetInnerHTML={{ __html: doc.html }} />
        <footer className="doc-footer">
          <div className="doc-pager">
            {prev ? (
              <a href={prev.route} rel="prev">
                Back: {prev.title}
              </a>
            ) : (
              <span />
            )}
            {next && (
              <a href={next.route} rel="next">
                Next: {next.title}
              </a>
            )}
          </div>
          <p>
            Something wrong or unclear on this page?{" "}
            <a href={`${site.repo}/edit/main/docs/${doc.file}`} rel="noopener">
              Change it on GitHub
            </a>
            .
          </p>
        </footer>
      </article>
      {doc.headings.length > 2 && (
        <nav className="doc-toc" aria-label="On this page">
          <p>On this page</p>
          <ul>
            {doc.headings.map((h) => (
              <li key={h.id}>
                <a href={`#${h.id}`}>{h.text}</a>
              </li>
            ))}
          </ul>
        </nav>
      )}
    </div>
  );
}
