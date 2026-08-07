# server_pal

Opinionated Axum router builder with batteries included.

## Features

- Per-IP rate limiting via `tower_governor` (default: 100 req/s, burst 200)
- Request logging via `tower_http::trace`
- Request body size limit (4MB)
- Response compression
- `Accept: application/json` header validation
- 10-second request timeout with `408` response
- Panic → 500 handler
- `GET /health` endpoint, exempt from rate limiting

## Usage

```rust
use server_pal::{RateLimit, listen_addr_pal, router_builder, serve};

let app = router_builder()
    .route("/my/v1/endpoint", post(my_handler))
    .rate_limit(Some(RateLimit { per_second: 10, burst: 20 })) // override default
    .build()
    .with_state(my_state);

serve(app, &listen_addr_pal()).await;
```

Use `serve()` (rather than `axum::serve` directly) so `tower_governor` can
extract peer IPs for per-IP rate limiting.

## HTTP metrics

`build()` wires the shared `http_server_*` family (requests, success,
failure, active gauge, microsecond duration histogram), labeled with
`service_name` (from `OTEL_SERVICE_NAME`), `http_method` (the nine RFC 9110
methods verbatim, anything else collapsed to `CUSTOM`), and — on the
counters and histogram — `route` (#1304): the matched Axum route template
(`/widgets/{id}`), the `/health` literal for the built-in health endpoint,
or the fixed `unmatched` sentinel for requests no route matched, so
scanners cannot mint unbounded series. The gauge alone carries no route: it
moves at request start; the counters and histogram move at completion (a
request abandoned mid-flight still counts, with its route, and records no
outcome). Instruments bind lazily on the first request, so `init_otel()`
just needs to have run by then — every main calls it before `serve`.
Descriptions, bucket bounds, and the route literals are pinned across the
Java/C++/Rust rails by `//domains/platform/libs/otel_contract`; label sets
and units are pinned by this crate's own tests against a real exporter.

## Rate limiting

The default limit is **100 req/s per IP, burst 200**. Override with `.rate_limit()`:

```rust
// Custom limit
.rate_limit(Some(RateLimit { per_second: 5, burst: 10 }))

// Disable entirely
.rate_limit(None)
```

Requests over the limit receive `429 Too Many Requests`. Rate-limited requests
are rejected before `TraceLayer`, so they won't appear in request logs.

`GET /health` is exempt: it is served from a separate router that the limiter
does not wrap. The container healthcheck probes on a fixed interval from one
IP, so a shared bucket drains and starts answering probes with `429` — the
service then reads as unhealthy while it is serving normally. Everything else
stays limited, the 404 fallback included, so spraying unknown paths still
costs quota. Requests rejected further in — a `406` from the `Accept` check, a
`408`, a panic — are counted against the limit too, because the limiter is the
outermost layer.

`per_second` is a rate — requests per second — and `burst` is how many may
arrive at once before that rate binds. Note that `tower_governor`'s own builder
takes a *replenish interval* under the same name, so `per_second(100)` there
means one request every 100 seconds; `RateLimit` converts, so the field means
what it says.

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8080` | Port to listen on (used by `listen_addr_pal()`) |
