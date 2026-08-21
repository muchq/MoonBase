# one_d4_v2

The C++ one_d4 API (#1389 phase 6): the first HTTP route served by the C++
detectors directly, with no Java in the path. One operation so far —
`POST /v2/analyze` — the v2 surface grows here until it replaces the Java
`one_d4` service entirely.

The name is permanent, not a migration artifact: "v2" is the API version the
routes carry (`/v2/analyze`), and the service keeps it after the Java service
is gone, the same way nobody renames a `/v2` path once clients hold it.

## What it is, and isn't

Analysis is stateless motif detection over a single PGN: parse, run the
detectors from `//domains/games/libs/one_d4_motifs` (the same `Extract` the
C++ indexing worker runs), answer. This service holds **no database credentials and no
corpus state** — indexing, querying, aggregation, retention and the schema all
stay with `one_d4` (#1332) until their own phases move them.

The wire shape is byte-for-byte the Java `/v1/analyze` response —
lowercase motif keys, absent-not-empty optional fields, uppercase `pinType` —
pinned by an exact whole-body golden in `wire_test.cc`. FORK is derived from
ATTACK rows at response time (two or more same-ply, same-attacker,
non-discovered attacks), exactly like every other read path; it is never an
extracted motif.

## Route and bounds

`POST /v2/analyze`, body `{"pgn": "..."}`, modeled in `model/one_d4_v2.smithy`
and served through the generated `OneD4V2Server` on the Beast transport.
Invalid PGN is a modeled 400 `InvalidPgnError`.

- **256KB PGN cap** (`kMaxPgnBytes`) — refused with a 400 past that; the
  transport's own 1MB body cap turns anything meaningfully larger into a 413
  before the handler runs.
- **4096-ply cap** (`kMaxPlies`) — the work bound. Java's version used a
  wall-clock timeout; this is deterministic instead, because detector cost
  tracks plies. The caveat: plies bound detector *iterations*, not literal
  time, so a pathological position could still be slower per ply — if that
  ever shows up in the latency histograms, the fix is profiling the detector,
  not restoring the wall clock.
- **20 requests/min per client IP**, sliding window, 429 with `retry-after: 60`.
  Client identity comes from the aura chain (`TRUSTED_PROXY_CIDRS` names who
  may assert `X-Forwarded-For` — Caddy in the deployment). Note mcpserver is
  one IP: all `analyze_position` MCP calls share one bucket (see the comment
  in `main.cc`).

## Run it

```bash
bazel run //domains/games/apis/one_d4_v2
curl localhost:8090/v2/analyze -H 'content-type: application/json' \
  -d '{"pgn":"1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7#"}'
```

Locally `TRUSTED_PROXY_CIDRS` may be unset (no proxy, peers key as
themselves); the deployment sets it to Caddy's static address.

## Deployment

Compose service `one_d4_v2`, port 8090, image `ghcr.io/muchq/one_d4_v2`
pinned by `ONE_D4_V2_SHA` (falling back to `DEPLOY_SHA`). It publishes the
network alias `one-d4-v2` for Java callers — `java.net.URI` nulls the host of
an underscored authority — while Caddy uses the service key. Two callers:

- **Caddy** routes public `POST /v2/analyze` straight through, no rewrite —
  the service serves the gateway path itself, so the route a client sees and
  the route the model declares are one string. This is the deliberate public
  exposure `deploy_config_test.go` records; auth remains open under #1332.
- **mcpserver** calls it directly (`ONE_D4_V2_BASE_URL`) for
  `analyze_position`.

256M memory where portrait gets 512M: portrait holds renders and a cache;
analysis is a parse and a fixed detector pass with nothing retained between
requests, and the ply cap bounds the largest transient.

Rolling deploys and rollbacks are safe: the route is stateless, so instances
on different versions can serve interleaved requests. No service serves
`/v1/analyze` — an mcpserver rollback past the v2 cutover points at a route
that answers nothing.

Metrics report as `service_name="one_d4_v2"` (standard aura `http_server_*`
instruments plus transport rejections) and the service has its own prom_proxy
registry entry — deliberately not merged into one_d4's
`one_d4(_worker)?` selectors, which cover indexing throughput, not serving.
