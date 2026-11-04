import { Footer } from "./components/Footer";
import { Header } from "./components/Header";
import { Doc } from "./pages/Doc";
import { Download } from "./pages/Download";
import { Follow } from "./pages/Follow";
import { Home } from "./pages/Home";
import { People } from "./pages/People";
import { Story } from "./pages/Story";
import type { PageData } from "./types";

function Body({ page }: { page: PageData }) {
  switch (page.kind) {
    case "home":
      return <Home />;
    case "story":
      return <Story />;
    case "people":
      return <People />;
    case "follow":
      return <Follow />;
    case "download":
      return <Download releases={page.releases} />;
    case "doc":
      return <Doc doc={page.doc} section={page.section} prev={page.prev} next={page.next} />;
    case "notfound":
      return (
        <article className="page-prose">
          <header className="page-head">
            <h1>There's no page here</h1>
            <p className="lede">
              The address may have a typo, or the page has moved. Start from the <a href="/">home page</a>, or look in
              the <a href="/guide/">user's guide</a>.
            </p>
          </header>
        </article>
      );
  }
}

export function App({ page }: { page: PageData }) {
  return (
    <>
      <a className="skip-link" href="#main">
        Skip to the page
      </a>
      <Header route={page.route} />
      <main id="main" className={page.kind === "home" ? "main home" : "main"}>
        <Body page={page} />
      </main>
      <Footer />
    </>
  );
}
