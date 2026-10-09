# PedalOne landing page

Static, responsive landing page. All deployable files are in `dist/`; no build step or backend is required.

## Preview locally

From this folder, run `python3 -m http.server 4173 --directory dist` and open http://localhost:4173.

## Edit

- `dist/index.html`: text, sections, links, and metadata.
- `dist/style.css`: layout, colors, and responsive styles.
- `dist/app.js`: screen selector and section navigation.
- `dist/assets/`: logo and product images, including animated GIFs.

## GitHub Pages and your GoDaddy domain

The `Deploy PedalOne landing page` workflow publishes only `landing-page/dist/` when that folder or the workflow changes on `main`. It can also be run manually from GitHub Actions. In Settings → Pages, select GitHub Actions as the publishing source. The radar portal is not published.

Set `pedal-one.com` as the custom domain in Pages settings, then configure its GoDaddy DNS records using GitHub's current custom-domain instructions. Enable Enforce HTTPS after the certificate is ready. Domain setup remains a separate manual step.

### Other hosting options

A domain registration alone does not include file hosting. Use a static hosting service or a hosting plan that accepts uploaded website files.

Upload the **contents** of `dist/` into the host's document root (often `public_html` on shared hosting). Keep `index.html`, `style.css`, `app.js`, and `assets/` together. For a Git-connected static host, set the publish directory to `landing-page/dist` and leave the build command empty.

Connect the domain using the DNS records supplied by your chosen host, and enable HTTPS there. Do not replace existing email-related DNS records. This repository does not configure DNS or hosting automatically.

The separate Sites publication is https://pedalone-ride-as-one.paulp3.chatgpt.site/. Changes pushed here do not automatically update that publication.

Google Fonts is the only external styling dependency. The page links to Instagram and includes no analytics, checkout, or backend. Pricing is a target; roadmap features are not launch promises.

## Email signup preview

The email signup form is a design preview only. It validates email format but does not store or transmit email addresses or subscribe visitors. Its visible warning and submission message intentionally make this clear.

Before enabling signup, connect a mailing-list provider such as MailerLite or Brevo, configure consent and confirmation/unsubscribe handling, and test the complete flow. Replace the preview submit handler in `dist/app.js` with the provider integration. Remove the preview warning only after the connection works; never embed private API keys in browser code.
