# PedalOne landing page

Static, responsive landing page. All deployable files are in `dist/`; no build step or backend is required.

## Preview locally

From this folder, run `python3 -m http.server 4173 --directory dist` and open http://localhost:4173.

## Edit

- `dist/index.html`: text, sections, links, and metadata.
- `dist/style.css`: layout, colors, and responsive styles.
- `dist/app.js`: screen selector and section navigation.
- `dist/assets/`: logo and product images, including animated GIFs.

## Hosting with your GoDaddy domain

A domain registration alone does not include file hosting. Use a static hosting service or a hosting plan that accepts uploaded website files.

Upload the **contents** of `dist/` into the host's document root (often `public_html` on shared hosting). Keep `index.html`, `style.css`, `app.js`, and `assets/` together. For a Git-connected static host, set the publish directory to `landing-page/dist` and leave the build command empty.

Connect the domain using the DNS records supplied by your chosen host, and enable HTTPS there. Do not replace existing email-related DNS records. This repository does not configure DNS or hosting automatically.

The separate Sites publication is https://pedalone-ride-as-one.paulp3.chatgpt.site/. Changes pushed here do not automatically update that publication.

Google Fonts is the only external styling dependency. The page links to Instagram and includes no analytics, forms, checkout, or backend. Pricing is a target; roadmap features are not launch promises.
