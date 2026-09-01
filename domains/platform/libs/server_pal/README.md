# server_pal

Opinionated Axum router builder with batteries included.

## Features

- Per-IP rate limiting via `tower_governor` (default: 100 req/s, burst 200)
- One JSON access-log line per request (#1459) — the metrics vocabulary
  (`http_method`, `route`, `service_name`) plus `target`, `status`,
  `duration_us`, `trace_id` and the raw `x_forwarded_for`. Binaries call
  `server_pal::init_logging()` to install the JSON subscriber; `RUST_LOG`
  still filters. (`tower_http::trace` stays in the stack for its ERROR
  event on failures.)
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

The default limit is **100 req/s, burst 200**. Override with `.rate_limit()`:

```rust
// Custom limit
.rate_limit(Some(RateLimit { per_second: 5.0, burst: 10 }))

// One request every ten seconds
.rate_limit(Some(RateLimit { per_second: 0.1, burst: 1 }))

// Disable entirely
.rate_limit(None)
```

Requests over the limit receive `429 Too Many Requests`, and the access log
records them — it sits outside the governor for exactly that reason. A
request the client abandons mid-flight is counted by the instruments (the
metrics guard fires on drop) but never logged: the access line is written
after the handler returns.

`per_second` is a rate — requests per second — and `burst` is how many may
arrive at once before that rate binds. `tower_governor`'s own builder takes a
*replenish interval* under the same name, so `per_second(100)` there means one
request every 100 seconds; `RateLimit` converts, so the field means what it
says.

### Buckets are keyed on the peer IP, which behind a proxy is the proxy

The default extractor reads the socket's peer address. Every service in
`deploy/consolidated` is reached only through Caddy, so external callers all
key on Caddy's container IP and **share one bucket** — a limit sized as though
it were per-user throttles everyone at once. The C++ rail keys off a declared
trust boundary instead (`TRUSTED_PROXY_CIDRS`, smithy-cpp ADR-0012); this rail
has not adopted that yet. Note that simply switching to `SmartIpKeyExtractor`
would make the key client-controlled, since Caddy appends to `X-Forwarded-For`
rather than replacing it.

### `GET /health` is exempt

It is served from a separate router the limiter does not wrap, matching the C++
rail, where the health endpoint runs ahead of the rate-limit guard.

This is defence in depth rather than a fix for anything currently reachable.
The container healthcheck connects over loopback from inside the container, so
it keys on `127.0.0.1` and never shared a bucket with client traffic. What made
probes fail was the units bug above: a bucket of 200 refilling once per 100
seconds, against a probe every 30 seconds, drained after a couple of hours and
then failed two probes in three — the service read as unhealthy while serving
normally. At a correct rate no probe cadence can drain it. The exemption keeps
the liveness signal off the quota path however the limit is later tuned or
keyed.

Everything else stays limited, the 404 fallback included, so spraying unknown
paths still costs quota. Requests rejected further in — a `406` from the
`Accept` check, a `408`, a panic — count against the limit too, because the
limiter is the outermost layer.

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8080` | Port to listen on (used by `listen_addr_pal()`) |
