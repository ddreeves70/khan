# sketchbillet.org

The website for SketchBillet: the home page, the story, the people, where to
follow and support it, and every guide in `docs/` turned into web pages. It's
React and TypeScript, built with Vite, and every page is built ahead of time
into plain HTML, so the site works without JavaScript and loads fast on a
school laptop.

## Run it on your computer

You need [Node.js](https://nodejs.org) 22 or newer.

```bash
cd website
npm install
npm run dev      # a live preview at http://localhost:5173 (home page only)
npm run build    # the whole site, every guide page, into dist/site
npm run preview  # look at what npm run build made, at http://localhost:4173
```

`npm run build` also checks every link and picture on every page, and fails
with a list if one is broken. Fix the page in `docs/`, not the checker.

## Where things are

| What | Where |
| --- | --- |
| Links to YouTube, TikTok, Instagram, X and Patreon, the email addresses, and the people page | `src/site.config.ts` |
| Home, Story, People, Follow and support | `src/pages/` |
| Header, footer, logo, the drawing at the top of the home page | `src/components/` |
| Colors, type and layout | `src/styles.css` |
| How `docs/` becomes the Guide, Tutorials, Command line, Plugins and Design pages | `scripts/docs.mjs` |
| Building each page, the sitemap and robots.txt | `scripts/prerender.mjs` |
| The link checker | `scripts/check-links.mjs` |

The guides aren't copied into the website: every build reads `docs/` again, so
a pull request that changes a guide changes the site too. Pictures in `docs/`
are copied to `/pictures/`.

### Adding your links

Open `src/site.config.ts` and fill in the empty strings, like:

```ts
social: {
  youtube: "https://www.youtube.com/@sketchbillet",
  ...
},
patreon: "https://www.patreon.com/sketchbillet",
email: { hello: "hello@sketchbillet.org", ... },
```

A link that's still empty doesn't appear anywhere, so the site never shows a
link that goes nowhere.

## Publishing

`.github/workflows/website.yml` builds the site on every pull request that
touches `website/` or `docs/`, and on `main` publishes it to GitHub Pages.

### Once: turn on GitHub Pages

1. On GitHub, open the repository's **Settings > Pages**.
2. Under **Build and deployment > Source**, pick **GitHub Actions**.
3. Under **Custom domain**, type `sketchbillet.org` and press **Save**.
   (The build also writes this into the site's `CNAME` file.)
4. When GitHub says the DNS check passed (this can take up to a day), tick
   **Enforce HTTPS**.

It's also worth verifying the domain, so nobody else can point a GitHub site at
it: your account's **Settings > Pages > Add a domain**, then add the TXT record
GitHub shows you.

### Once: DNS at your domain registrar

For **sketchbillet.org**, add these records (delete any other `A` or `AAAA`
records for `@` first, like a registrar's "parked" page):

| Type | Name | Value |
| --- | --- | --- |
| A | @ | 185.199.108.153 |
| A | @ | 185.199.109.153 |
| A | @ | 185.199.110.153 |
| A | @ | 185.199.111.153 |
| AAAA | @ | 2606:50c0:8000::153 |
| AAAA | @ | 2606:50c0:8001::153 |
| AAAA | @ | 2606:50c0:8002::153 |
| AAAA | @ | 2606:50c0:8003::153 |
| CNAME | www | ddreeves70.github.io |

GitHub sends `www.sketchbillet.org` to `sketchbillet.org` by itself.

For **sketchbillet.com**, use your registrar's **forwarding** (sometimes called
a redirect): forward `sketchbillet.com` and `www.sketchbillet.com` to
`https://sketchbillet.org`, as a permanent (301) redirect, keeping the path if
it offers that. One GitHub Pages site can only have one domain, so the `.com`
can't point at GitHub directly.

To check the records worked, run `dig sketchbillet.org +short` (macOS, Linux)
or `nslookup sketchbillet.org` (Windows): it should print the four
`185.199.x.153` numbers.

### Email

GitHub Pages doesn't do email. To get `hello@sketchbillet.org`, turn on your
registrar's email forwarding (most include it free) to send it on to your own
inbox, or use an email host. Either one adds `MX` records; they don't change the
records above. Then put the addresses in `src/site.config.ts`.
