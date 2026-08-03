# OpenTelemetry Integration

Simplified OpenTelemetry metrics support for C++ services.

## Overview

This module provides:

- **OtelProvider**: Initializes OpenTelemetry with OTLP HTTP export
- **MetricsRecorder**: High-level API for recording metrics
- **HttpMetricsManager**: The shared HTTP serving instruments
  (`http_server_requests`, `http_server_requests_active`,
  `http_server_request_duration`, `http_server_requests_success` /
  `_failure`), transport-agnostic so every service exports the same names
  and labels
- **http_instrument_descriptions.h**: The descriptions those shared
  instruments are exported with, pinned equal to the Java and Rust rails

## Quick Start

```cpp
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/futility/otel/metrics.h"

using namespace futility::otel;

int main() {
    // 1. Initialize OpenTelemetry (once at startup)
    OtelConfig config{
        .service_name = "my-service",
        .service_version = "1.0.0",
        .otlp_endpoint = "http://otel-collector:4318/v1/metrics",
        .export_interval = std::chrono::seconds(30),
        .enable_metrics = true
    };
    OtelProvider provider(config);

    // 2. Create a metrics recorder
    MetricsRecorder metrics("my-service");

    // 3. Record metrics
    metrics.RecordCounter("requests_total", 1, {{"method", "GET"}});
    metrics.RecordLatency("request_duration_us", std::chrono::microseconds(150));
    metrics.RecordGauge("connections_active", 42);

    // Provider lives for application lifetime
    RunServer();
    return 0;
}
```

## Components

### OtelProvider

Manages OpenTelemetry lifecycle:

- Creates and configures the OTLP HTTP metric exporter
- Sets up periodic metric export
- Registers as the global meter provider
- Cleans up on destruction

```cpp
OtelConfig config{
    .service_name = "my-service",      // Reported in metrics
    .service_version = "1.0.0",        // Reported in metrics
    .otlp_endpoint = "http://...",     // OTLP collector endpoint
    .export_interval = std::chrono::seconds(30),
    .enable_metrics = true             // Set false to disable
};

OtelProvider provider(config);
```

**Environment Variables:**

- `OTEL_EXPORTER_OTLP_ENDPOINT`: Overrides `config.otlp_endpoint`

### MetricsRecorder

High-level API for recording metrics without managing instruments directly.

#### Counter (Monotonic)

For counting events that only increase:

```cpp
// Simple increment
metrics.RecordCounter("http_requests_total");

// Increment by value
metrics.RecordCounter("bytes_processed", request.size());

// With attributes
metrics.RecordCounter("http_requests_total", 1, {
    {"method", "POST"},
    {"status", "200"},
    {"path", "/api/users"}
});
```

#### Histogram (Latency)

For timing distributions:

```cpp
auto start = std::chrono::steady_clock::now();
ProcessRequest();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - start);

metrics.RecordLatency("request_duration_us", duration, {
    {"method", "GET"},
    {"endpoint", "/api/data"}
});
```

#### Gauge (Point-in-Time)

For current values that can go up or down:

```cpp
metrics.RecordGauge("connections_active", connection_count);
metrics.RecordGauge("queue_depth", queue.size());
metrics.RecordGauge("memory_used_bytes", GetMemoryUsage());
```

## Metric Naming Conventions

Follow [OpenTelemetry semantic conventions](https://opentelemetry.io/docs/specs/semconv/):

| Type | Convention | Example |
|------|------------|---------|
| Counter | `<noun>_total` | `http_requests_total` |
| Histogram | `<noun>_<unit>` | `request_duration_us`, `response_size_bytes` |
| Gauge | `<noun>_<state>` | `connections_active`, `queue_depth` |

## Disabling Metrics

To disable metrics (e.g., in tests or local development):

```cpp
OtelConfig config{
    .enable_metrics = false
};
OtelProvider provider(config);  // No-op, no metrics exported
```

## Infrastructure Requirements

Metrics are exported via OTLP HTTP to a collector. Common setups:

1. **OpenTelemetry Collector**: Receives, processes, and exports to backends
2. **Prometheus + OTLP Receiver**: Direct Prometheus scraping via OTLP
3. **Grafana Cloud/Datadog/etc.**: Cloud observability platforms

Example collector endpoint: `http://otel-collector:4318/v1/metrics`

### HttpMetricsManager

Per-request HTTP serving metrics with `service_name`/`route`/`method` labels
(plus `status_code`, `result`, and `error_type` on completions). Call
`RecordRequestStart` at ingress and `RecordRequestComplete` when the response
is written; the pair keeps the request counter and active gauge symmetric.

```cpp
#include "domains/platform/libs/futility/otel/http_metrics.h"

futility::otel::HttpMetricsManager http_metrics("my-service");
http_metrics.RecordRequestStart("/api/users", "GET");
// ... serve ...
http_metrics.RecordRequestComplete("/api/users", "GET", 200, duration_us);
```

## Instrument Descriptions

The `http_server_*` instruments are reported by services in all three
languages, and every one of them lands in the same collector. The collector
merges series by instrument name and keeps the **first** description it sees,
logging

```
Instrument description conflict, using existing
```

for every later one that disagrees — once per export interval, for as long as
both services run. An empty description conflicts with a non-empty one just as
loudly, so declining to describe an instrument is not a way to stay out of it.

`MetricsRecorder` therefore looks each instrument up in
`http_instrument_descriptions.h` at creation, and
`RegisterLatencyBucketView` leaves the view's description empty so it does not
stamp one sentence over every `*_microseconds` histogram it matches.
Service-defined instruments get no description, which is correct: only one
service reports them, so there is nothing to conflict with.

The table is pinned equal to yodel's and server_pal's by
[`//domains/platform/libs/otel_contract`](../../otel_contract). Changing a
description means changing it in all three.

## Bazel Targets

```python
deps = [
    "//domains/platform/libs/futility/otel:otel_provider",
    "//domains/platform/libs/futility/otel:metrics",
    "//domains/platform/libs/futility/otel:http_metrics",
    "//domains/platform/libs/futility/otel:http_instrument_descriptions",
]
```
