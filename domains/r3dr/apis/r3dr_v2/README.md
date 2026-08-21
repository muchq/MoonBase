# r3dr_v2

The C++ URL shortener (#1359), replacing the Go service in `../r3dr`. Serves
`/r3dr/v1/*` behind `api.muchq.com` beside the Go binary (which keeps
`r3dr.net` until deprecation). Separate databases, no shared state.

## Routes

- `POST /r3dr/v1/shorten` → 201 `{"slug":"..."}`. `longUrl`: 11–1000 chars,
  `http://`/`https://`, trait-validated. `expiresAt` (epoch millis)
  required: in the future, ceiling 30d.
- `GET /r3dr/v1/r/{slug}` → 302 with `Location`. Expiry enforced in the SQL
  and in the cache entry. Unknown/expired/too-short slugs: one modeled JSON
  404. Store failure: 500, not 404.
- `/health` via aura.

The API returns a bare slug; the short link is
`https://api.muchq.com/r3dr/v1/r/{slug}` until a short domain fronts it.
CORS for `muchq.com` is already set at the api.muchq.com Caddy block.

Error shapes: 404s and clock-rule 400s are modeled JSON (`{"message":...}`);
trait 400s use the generated `{"fieldList":[...],"message":...}` shape.

Slugs are the Go encoder's, bit-exact: little-endian id bytes (2/4/8,
stepping at MaxInt16/MaxInt32), base64url unpadded → 3/6/11 chars. No
decoder exists; `encoding_test.cc` carries the Go vectors as the pin.

Not here: UI (#1359 chunk 2, muchq.com), stats, vanity slugs, the expired-row
sweep (#373 — `idx_urls_expires_at` is in place), negative caching (the LRU
has no invalidation; the per-client rate limit is the scan defense).

## Storage

Own `r3dr_v2` database/role on `shared_postgres`, provisioned by the
`r3dr_v2_db_init` one-shot from `R3DR_V2_DB_PASSWORD` — a new `~/.env`
secret; compose refuses to start without it. Schema applied at startup by `migrations.cc`
(idempotent, fail-fast) and in every DB test's SetUp. Two `pg::Client`s so
redirects don't queue behind shortens; inserts are `ON CONFLICT DO NOTHING`
so the client's reconnect-retry can't mint duplicates.

## Run it

```bash
R3DR_V2_DB_URL=postgresql://user:pass@localhost:5432/r3dr_v2 \
  bazel run //domains/r3dr/apis/r3dr_v2
curl localhost:8091/r3dr/v1/shorten -H 'content-type: application/json' \
  -d '{"longUrl":"https://example.com/some/where"}'
curl -i localhost:8091/r3dr/v1/r/AQA
```

`bazel test //domains/r3dr/apis/r3dr_v2/...` — `pg_url_store_test` skips
silently without `PG_TEST_DB_URL` (CI supplies it).

## Deployment

`ghcr.io/muchq/r3dr_v2` (pin `R3DR_V2_SHA`), port 8091, alias `r3dr-v2`.
Caddy proxies exactly the two operation routes, unrewritten.
`TRUSTED_PROXY_CIDRS` anchors the per-client limit (120/min, bounded keys)
to real client addresses. 0.25 CPU / 256M. Redeploys are safe: boot
migrations are idempotent.
