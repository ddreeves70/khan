import { useState } from "react";
import { Wordmark } from "./Logo";

const LINKS = [
  { href: "/guide/", text: "Guide" },
  { href: "/tutorials/", text: "Tutorials" },
  { href: "/cli/", text: "Command line" },
  { href: "/plugins/", text: "Plugins" },
  { href: "/story/", text: "Story" },
  { href: "/people/", text: "People" },
  { href: "/follow/", text: "Follow and support" },
  { href: "/download/", text: "Download" },
];

// The bar across the top. On a phone the links fold into a menu button.
export function Header({ route }: { route: string }) {
  const [open, setOpen] = useState(false);
  return (
    <header className="site-header">
      <div className="site-header-inner">
        <Wordmark />
        <button
          className="menu-button"
          type="button"
          aria-expanded={open}
          aria-controls="site-nav"
          onClick={() => setOpen(!open)}
        >
          {open ? "Close menu" : "Menu"}
        </button>
        <nav id="site-nav" className={open ? "site-nav open" : "site-nav"} aria-label="Main">
          {LINKS.map((l) => (
            <a
              key={l.href}
              href={l.href}
              className={l.href === "/download/" ? "nav-download" : undefined}
              aria-current={route.startsWith(l.href) ? "page" : undefined}
            >
              {l.text}
            </a>
          ))}
        </nav>
      </div>
    </header>
  );
}
