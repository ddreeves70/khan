import "@fontsource/barlow/400.css";
import "@fontsource/barlow/500.css";
import "@fontsource/barlow/600.css";
import "@fontsource/barlow-condensed/500.css";
import "@fontsource/barlow-condensed/600.css";
import "@fontsource/ibm-plex-mono/400.css";
import "./styles.css";
import { hydrateRoot } from "react-dom/client";
import { App } from "./App";

// The page is already there, built ahead of time; this makes its buttons work.
const root = document.getElementById("root");
if (root && window.__PAGE__) hydrateRoot(root, <App page={window.__PAGE__} />);
