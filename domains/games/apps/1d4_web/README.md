# 1d4 Web

Lightweight web frontend for the [one_d4](https://github.com/muchq/MoonBase/tree/main/domains/games/apis/one_d4) chess game indexer. No framework — vanilla HTML, CSS, and ES modules. Deploys to **Cloudflare Workers** at [1d4.net](https://1d4.net).

## Features

- **Games** — Browse indexed games with sortable columns (player, ELO, time class, ECO, result, motifs), motif badges, and click-through to chess.com. Pagination.
- **Index** — Enqueue index requests (username, platform chess.com, start/end month). Submit calls `POST https://api.1d4.net/v1/index`. View recent request status with auto-poll via `GET https://api.1d4.net/v1/index/{id}`.
- **Query** — ChessQL query input with syntax help and example chips; results table and limit selector.

## Run locally

Serve the `src/` directory over HTTP (required for ES modules). For example:

```bash
cd src && python3 -m http.server 8080
```

Then open `http://localhost:8080`. The app will call `https://api.1d4.net`; ensure CORS is configured for your origin (production uses `https://1d4.net`).

## Deploy (Cloudflare Workers)

1. Install [Wrangler](https://developers.cloudflare.com/workers/wrangler/install-and-update/).
2. From this directory: `wrangler deploy`.
3. Configure the domain (e.g. `1d4.net`) in the Cloudflare dashboard or via `wrangler.toml` routes.

## Build (Bazel)

- `bazel build //domains/games/apps/1d4_web:static_assets` — filegroup of static files.
- `bazel build //domains/games/apps/1d4_web:1d4_web_assets` — tar of static assets for deployment.

## Dependencies

- Requires the one_d4 API deployed at `api.1d4.net` (see [#1028](https://github.com/muchq/MoonBase/issues/1028)).
- API CORS must allow origin `https://1d4.net`.
