# iili

The C++ URL shortener (#1359). Serves `/iili/v1/*` behind `api.muchq.com`;
short links resolve on `i.iili.uk`, which Caddy rewrites onto that path.

## Routes

- `POST /iili/v1/shorten` → 201 `{"slug":"..."}`. `longUrl`: 11–1000 chars,
  `http://`/`https://`, trait-validated. `expiresAt` (epoch millis)
  required: in the future, ceiling 30d.
- `GET /iili/v1/r/{slug}` → 302 with `Location`. Expiry enforced in the SQL
  and in the cache entry. Unknown, expired, or non-slug-shaped: one modeled JSON 404. Store failure: 500, not 404.
- `HEAD /iili/v1/r/{slug}` → the same 302 and `Location`. Its own operation,
  since the router buckets by exact method. The transport sends the GET's
  `Content-Length` and no octets.
- `/health` via aura.

The API returns a bare slug; [`iili_web`](../../apps/iili_web) fronts it as
`https://i.iili.uk/r/{slug}` (SPA at iili.uk on Cloudflare; redirects on
Caddy at i.iili.uk), and muchq.com/iili mints the same links. CORS for
`muchq.com` and `iili.uk` is set at the api.muchq.com Caddy block.

Error shapes: 404s and clock-rule 400s are modeled JSON (`{"message":...}`);
trait 400s use the generated `{"fieldList":[...],"message":...}` shape.

Slugs are the Go encoder's, bit-exact: little-endian id bytes (2/4/8,
stepping at MaxInt16/MaxInt32), base64url unpadded → 3/6/11 chars. No
decoder exists; `encoding_test.cc` carries the Go vectors as the pin.

Not here: stats, vanity slugs, the expired-row sweep (#373 —
`idx_urls_expires_at` is in place), negative caching (the LRU has no
invalidation; the per-client rate limit is the scan defense).

## Storage

Own database and role on `shared_postgres`, both still named `r3dr_v2` and
provisioned by the `iili_db_init` one-shot from `R3DR_V2_DB_PASSWORD` in
`~/.env` — renaming a role and database holding live rows is an operation,
not a rename. Compose refuses to start without the secret. Schema applied at
startup by `migrations.cc` (idempotent, fail-fast) and in every DB test's SetUp. Two `pg::Client`s so
redirects don't queue behind shortens. Inserts are `ON CONFLICT (id) DO
UPDATE … RETURNING (xmax = 0)`: a conflict row identical to ours is the
client's reconnect-retry replaying (success, same slug); any other row means
the sequence is behind the table (an import without `setval`) and the insert
refuses rather than alias a slug.

## Run it

```bash
IILI_DB_URL=postgresql://user:pass@localhost:5432/r3dr_v2 \
  bazel run //domains/iili/apis/iili
curl localhost:8091/iili/v1/shorten -H 'content-type: application/json' \
  -d "{\"longUrl\":\"https://example.com/some/where\",\"expiresAt\":$(( ($(date +%s) + 3600) * 1000 ))}"
curl -i localhost:8091/iili/v1/r/AQA
```

`bazel test //domains/iili/apis/iili/...` — `pg_url_store_test` skips
silently without `PG_TEST_DB_URL` (CI supplies it).

## Deployment

On `api.muchq.com`, Caddy proxies the two operation routes unrewritten.
On `i.iili.uk`, Caddy rewrites `GET|HEAD /r/{slug}` onto
`/iili/v1/r/{slug}` before the same upstream. `TRUSTED_PROXY_CIDRS`
anchors the per-client limit (120/min, bounded keys) to real client
addresses. 0.25 CPU / 256M. Redeploys are safe: boot migrations are
idempotent.
