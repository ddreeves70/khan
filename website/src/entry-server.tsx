import { renderToString } from "react-dom/server";
import { App } from "./App";
import type { PageData } from "./types";

// Renders one page to HTML, at build time.
export function render(page: PageData): string {
  return renderToString(<App page={page} />);
}
