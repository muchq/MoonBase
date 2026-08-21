# iili

React + TypeScript frontend for the [r3dr_v2](../../apis/r3dr_v2) URL
shortener. Built on the 1d4_web stack (Vite, Tailwind, Vitest) and deployed
to **Cloudflare Workers** at [iili.uk](https://iili.uk) — the shortener's
SPA. A second frontend for the same API lives at muchq.com/r3dr (the
muchq.github.io repo); both mint `i.iili.uk/r/{slug}` links.

## What it does

- **Shorten** — paste a link (bare domains get `https://` for free), pick an
  expiry (1 hour / 1 day / 7 days / 30 days — the top one sits just under
  the API's ceiling, on purpose: a fast client clock must not turn it into
  a 400), copy the result. The last five links live in `localStorage`;
  nothing is tracked server-side.
- **Redirect** — short links resolve on `i.iili.uk` (A-record to the
  consolidated host). Caddy rewrites `GET|HEAD /r/{slug}` onto
  `r3dr_v2`'s `/r3dr/v2/r/{slug}` so the API's per-client rate limit still
  keys on the real client. This Worker is SPA-only. (`/r/`, not bare
  `/{slug}`: asset paths like `/assets` are themselves slug-shaped.)

The browser calls `https://api.muchq.com` directly; CORS for this origin is
set in the consolidated Caddyfile. `VITE_API_BASE` overrides the API for
local experiments.

## Develop locally

```bash
npm install
npm run dev        # Vite dev server with the Workers runtime
```

## Test & typecheck

```bash
npm test           # Vitest
npm run typecheck  # tsc --noEmit
```

## Build

```bash
npm run build
# Outputs:
#   dist/client/       — SPA static assets (served by Workers Assets)
#   dist/iili_web/     — Worker bundle + generated wrangler.json
```

## Deploy (Cloudflare Workers)

```bash
npm run build
npx wrangler deploy --config dist/iili_web/wrangler.json
```

Cloudflare CI is configured in the Workers dashboard:
- **Build command:** `npm ci && npm run build`
- **Deploy command:** `npx wrangler deploy --config dist/iili_web/wrangler.json`
- **Root directory:** `/domains/r3dr/apps/iili_web`

`iili.uk` is the SPA; `i.iili.uk` is the redirect edge on the consolidated
host. The old `r3dr.net` (Go service, own database) retires separately in
#1359 chunk 3, and its links die with it.

To smoke-test before custom domains attach: the `workers.dev` preview
exercises the static UI, but can't mint — API CORS echoes only `iili.uk`,
`muchq.com`, and `localhost:5173`. Mint from `npm run dev` with
`VITE_SHORT_LINK_BASE=https://i.iili.uk/r/` (or a local Caddy) so the
minted links hit the redirect host.

## Dependencies

- The r3dr_v2 API deployed at `api.muchq.com`.
- Redirect host `i.iili.uk` on the consolidated Caddy (rewrite → r3dr_v2).
- API CORS must allow origin `https://iili.uk` (and `http://localhost:5173`
  for `npm run dev`) — both set in `deploy/consolidated/Caddyfile`.
