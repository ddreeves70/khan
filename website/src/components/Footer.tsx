import { site } from "../site.config";
import { Mark } from "./Logo";

// Where SketchBillet is elsewhere: the links that are filled in, in this order.
export function socialLinks() {
  const out: { label: string; href: string }[] = [];
  if (site.social.youtube) out.push({ label: "YouTube", href: site.social.youtube });
  if (site.social.tiktok) out.push({ label: "TikTok", href: site.social.tiktok });
  if (site.social.instagram) out.push({ label: "Instagram", href: site.social.instagram });
  if (site.social.x) out.push({ label: "X", href: site.social.x });
  if (site.patreon) out.push({ label: "Patreon", href: site.patreon });
  out.push({ label: "GitHub", href: site.repo });
  return out;
}

export function Footer() {
  return (
    <footer className="site-footer">
      <div className="site-footer-inner">
        <div className="footer-about">
          <Mark size={22} />
          <p>
            SketchBillet is free and open source, under the Apache 2.0 license. No ads, no accounts, and your
            designs stay on your computer.
          </p>
        </div>
        <nav className="footer-links" aria-label="SketchBillet elsewhere">
          {socialLinks().map((l) => (
            <a key={l.label} href={l.href} rel="noopener">
              {l.label}
            </a>
          ))}
          <a href={site.discussions} rel="noopener">
            Discussions
          </a>
          {site.email.hello && <a href={`mailto:${site.email.hello}`}>{site.email.hello}</a>}
        </nav>
      </div>
    </footer>
  );
}
