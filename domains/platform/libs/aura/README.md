# Aura

The serving chain for C++ services on [smithy-cpp](https://github.com/aaylward/smithy-cpp):
observability, health, and optional per-client rate limiting, composed the
same way in production and in tests.

## What it gives you

- **`ProductionChain(ChainOptions, handler)`** — the one entry point:
  - `ServingObservability` outermost, so health probes and 429s are
    observed too: the shared `http_server_*` instruments
    (`futility/otel:http_metrics`) plus one access-log line per request
    with the W3C `trace_id=` from the request's traceparent (smithy-cpp
    ADR-0011)
  - `HealthEndpoint("/health")` before the guard, so probes are never rate
    limited
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
