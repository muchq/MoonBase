# r3dr_v2

The C++ URL shortener (#1359), replacing the Go service in `../r3dr`. Serves
`/r3dr/v2/*` behind `api.muchq.com` beside the Go binary (which keeps
`r3dr.net` until deprecation). Separate databases, no shared state.

## Routes

- `POST /r3dr/v2/shorten` → 201 `{"slug":"..."}`. `longUrl`: 11–1000 chars,
  `http://`/`https://`, trait-validated. `expiresAt` (epoch millis)
  required: in the future, ceiling 30d.
- `GET /r3dr/v2/r/{slug}` → 302 with `Location`. Expiry enforced in the SQL
  and in the cache entry. Unknown, expired, or non-slug-shaped: one modeled JSON 404. Store failure: 500, not 404.
- `/health` via aura.

The API returns a bare slug; [`r3dr_web_v2`](../../apps/r3dr_web_v2) fronts
it as `https://r3dr.net/r/{slug}`, 302ing to this API. CORS for `muchq.com`
and `r3dr.net` is set at the api.muchq.com Caddy block.

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
redirects don't queue behind shortens. Inserts are `ON CONFLICT (id) DO
UPDATE … RETURNING (xmax = 0)`: a conflict row identical to ours is the
client's reconnect-retry replaying (success, same slug); any other row means
the sequence is behind the table (an import without `setval`) and the insert
refuses rather than alias a slug.

## Run it

```bash
R3DR_V2_DB_URL=postgresql://user:pass@localhost:5432/r3dr_v2 \
  bazel run //domains/r3dr/apis/r3dr_v2
curl localhost:8091/r3dr/v2/shorten -H 'content-type: application/json' \
  -d "{\"longUrl\":\"https://example.com/some/where\",\"expiresAt\":$(( ($(date +%s) + 3600) * 1000 ))}"
curl -i localhost:8091/r3dr/v2/r/AQA
```

`bazel test //domains/r3dr/apis/r3dr_v2/...` — `pg_url_store_test` skips
silently without `PG_TEST_DB_URL` (CI supplies it).

## Deployment

Caddy proxies exactly the two operation routes, unrewritten.
`TRUSTED_PROXY_CIDRS` anchors the per-client limit (120/min, bounded keys)
to real client addresses. 0.25 CPU / 256M. Redeploys are safe: boot
migrations are idempotent.
