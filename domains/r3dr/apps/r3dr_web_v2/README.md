# r3dr Web v2

React + TypeScript frontend for the [r3dr_v2](../../apis/r3dr_v2) URL
shortener. Built on the 1d4_web stack (Vite, Tailwind, Vitest) and deployed
to **Cloudflare Workers** at [r3dr.net](https://r3dr.net), replacing the VM
that served the Go service and its static page.

## What it does

- **Shorten** — paste a link (bare domains get `https://` for free), pick an
  expiry (1 hour / 1 day / 7 days / 30 days — the API's ceiling), copy the
  result. The last five links live in `localStorage`; nothing is tracked
  server-side.
- **Redirect** — the worker 302s `GET /r/{slug}` — v1's short-link shape — to
  `api.muchq.com/r3dr/v1/r/{slug}`, so the API's per-client rate limit still
  keys on the real client. Everything else is the SPA.

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
#   dist/r3dr_web/     — Worker bundle + generated wrangler.json
```

## Deploy (Cloudflare Workers)

```bash
npm run build
npx wrangler deploy --config dist/r3dr_web/wrangler.json
```

Cloudflare CI is configured in the Workers dashboard:
- **Build command:** `npm ci && npm run build`
- **Deploy command:** `npx wrangler deploy --config dist/r3dr_web/wrangler.json`
- **Root directory:** `/domains/r3dr/apps/r3dr_web_v2`

Pointing the `r3dr.net` custom domain at this worker is the cutover: the Go
VM stops receiving traffic, and slugs minted by v1 (stored in its own
database) stop resolving — as 404s, not misdirections: v2's migrations floor
its `url_ids` sequence above v1's lifetime id space, since both generations
derive the same slug from the same id. That retirement is #1359 chunk 3.

To smoke-test on a `workers.dev` preview before the flip, build with
`VITE_SHORT_LINK_BASE=https://r3dr-web.<account>.workers.dev/r/` — minted
`r3dr.net` links would otherwise still resolve through the old stack.

## Dependencies

- The r3dr_v2 API deployed at `api.muchq.com`.
- API CORS must allow origin `https://r3dr.net` (and `http://localhost:5173`
  for `npm run dev`) — both set in `deploy/consolidated/Caddyfile`.
