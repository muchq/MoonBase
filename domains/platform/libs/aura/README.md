# Aura

The serving-tier components for C++ services on
[smithy-cpp](https://github.com/muchq/smithy-cpp): observability, health,
per-client rate limiting, and caching, composed the same way in production
and in tests.

Two targets, deliberately separate:

| Target | Contents | Pulls Beast? |
|---|---|---|
| `:aura` | the serving chain (`middleware.h`) | yes |
| `:cache` | `aura::Cache` (`cache.h`) | no |

A library that wants a cache should not inherit an HTTP transport. That is
the whole of the split — `:cache` still reaches opentelemetry-cpp via
`futility/otel`, so it is not a target you can build behind a blocking proxy
without `scripts/make-git-overrides.sh`.

## What the chain gives you

- **`ProductionChain(ChainOptions, handler)`** — the one entry point:
  - `ServingObservability` outermost, so health probes and 429s are
    observed too: the shared `http_server_*` instruments
    (`futility/otel:http_metrics`) plus one access-log line per request —
    a single JSON object carrying the metrics vocabulary plus the raw
    target and the W3C `trace_id` from the request's traceparent
    (smithy-cpp ADR-0011). The route label is bounded (#1305): the matched Smithy
    operation name the generated router stamps on its responses,
    `kHealthRoute` for the health endpoint, or the `kUnmatchedRoute`
    sentinel — never the raw request path. The method label is bounded the
    same way: the nine RFC 9110 methods verbatim, any other wire token
    collapsed to `CUSTOM`
  - `HealthEndpoint(kHealthRoute)` before the guard, so probes are never
    rate limited
  - `PerClientRateLimit` keyed on the ADR-0012 derived client address
    (trust boundary from `ChainOptions::trusted_proxies`), answering 429
    with Retry-After — skipped entirely when `allow_request` is unset
- **`RejectionMetrics`** — `BeastServerTransport::Options::on_rejected`
  adapter so transport-written 413/431s land in the same instruments
- **`ConnectionEventLog`** — `on_connection_event` observer (ADR-0013): one
  WARNING line per connection the transport terminates without a response

## Usage

```cpp
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"

auto metrics = aura::MakeHttpMetricsSink(
    std::make_shared<futility::otel::HttpMetricsManager>("my-service"));

auto handler = aura::ProductionChain(
    aura::ChainOptions{
        .metrics = metrics,
        .allow_request = [limiter](const std::string& c) { return limiter->allow(c); },
        .trusted_proxies = trusted_proxies},
    server.Handler());

smithy::http::BeastServerTransport::Options options;
options.on_rejected = aura::RejectionMetrics(metrics);
options.on_connection_event = aura::ConnectionEventLog();
```

Consumers: `//domains/graphics/apis/portrait`, `//domains/games/apis/golf_hub`.
`HttpMetricsSink` is a virtual seam — tests inject a recording sink instead
of the OTel-backed one.

## `aura::Cache`

A fixed-capacity LRU cache that counts its own hits and misses, emitting the
standard `cache_hits_total` / `cache_misses_total{service_name, cache}` family
([#1209](https://github.com/muchq/MoonBase/issues/1209)). Storage behavior is
`futility/cache`'s; what aura adds is that the metric arrives by picking this
type rather than by remembering to count in each branch of a lookup.

```cpp
#include "domains/platform/libs/aura/cache.h"

aura::Cache<TraceRequest, std::vector<std::uint8_t>> cache_{"trace", 50, metrics};

if (auto hit = cache_.get(request)) return *hit;   // counted either way
```

The `service_name` label comes from the `MetricsRecorder` you pass, not from a
second argument, so a cache series cannot be labeled for a different service
than the meter it was recorded through. Capacity 0 stores nothing and reports
every lookup as a miss, which turns the cache off without disturbing its call
sites or leaving a hole in the dashboard.

Counter names are recorded without `_total`; the OTLP exporter appends it, as
it does for `http_server_requests`.
