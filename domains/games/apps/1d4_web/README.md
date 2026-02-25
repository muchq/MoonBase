# 1d4 Web

React + TypeScript frontend for the [one_d4](https://github.com/muchq/MoonBase/tree/main/domains/games/apis/one_d4) chess game indexer. Built with Vite 6 and deployed to **Cloudflare Workers** at [1d4.net](https://1d4.net).

## Features

- **Games** — Browse indexed games with sortable columns (player, ELO, time class, ECO, result, motifs), motif badges with occurrence tooltips, and click-through to chess.com. Pagination.
- **Index** — Enqueue index requests (username, platform, start/end month). Auto-polls `GET /v1/index` while any request is pending/processing.
- **Query** — ChessQL query input with syntax help and example chips; results table and limit selector.

## Develop locally

```bash
npm install
npm run dev        # Vite dev server with Cloudflare Workers runtime
```

The app calls `https://api.1d4.net`. CORS on that API allows `localhost` in development.

## Test & typecheck

```bash
npm test           # Vitest (30 tests)
npm run typecheck  # tsc --noEmit
```

## Build

```bash
npm run build
# Outputs:
#   dist/client/        — SPA static assets (served by Workers Assets)
#   dist/1d4_web/       — Worker bundle + generated wrangler.json
```

## Deploy (Cloudflare Workers)

```bash
npm run build
npx wrangler deploy --config dist/1d4_web/wrangler.json
```

Cloudflare CI is configured in the Workers dashboard:
- **Build command:** `npm ci && npm run build`
- **Deploy command:** `npx wrangler deploy --config dist/1d4_web/wrangler.json`
- **Root directory:** `/domains/games/apps/1d4_web`

## Build (Bazel)

Run `npm run build` first (Bazel doesn't invoke npm), then:

```bash
bazel build //domains/games/apps/1d4_web:1d4_web_assets
```

This tars `dist/` for CI artifact storage.

## Dependencies

- Requires the one_d4 API deployed at `api.1d4.net`.
- API CORS must allow origin `https://1d4.net`.
