# Futility

A collection of C++ utility libraries for building services.

## Modules

| Module | Description | Thread-Safe |
|--------|-------------|-------------|
| [rate_limiter](rate_limiter/) | Per-key sliding window and token bucket rate limiters | Yes |
| [cache](cache/) | LRU cache with automatic eviction | No |
| [otel](otel/) | OpenTelemetry metrics integration | Yes |
| [status](status/) | gRPC/Abseil status conversion utilities | Yes |
| [base64](base64/) | Base64 encoding/decoding for binary data | Yes |

## Quick Start

### Rate Limiting

```cpp
#include "cpp/futility/rate_limiter/sliding_window_rate_limiter.h"

using namespace futility::rate_limiter;

SlidingWindowRateLimiterConfig config{
    .max_requests_per_key = 100,
    .window_size = std::chrono::minutes(1)
};
SlidingWindowRateLimiter<std::string> limiter(config);

if (!limiter.allow(client_ip)) {
    return HttpResponse(429, "Too Many Requests");
}
```

### Caching

```cpp
#include "cpp/futility/cache/lru_cache.h"

futility::cache::LRUCache<std::string, Result> cache(1000);

auto cached = cache.get("key");
if (!cached) {
    auto result = ComputeExpensive("key");
    cache.insert("key", result);
    return result;
}
return *cached;
```

### Metrics

```cpp
#include "cpp/futility/otel/otel_provider.h"
#include "cpp/futility/otel/metrics.h"

using namespace futility::otel;

OtelProvider provider({.service_name = "my-service"});
MetricsRecorder metrics("my-service");

metrics.RecordCounter("requests_total", 1, {{"method", "GET"}});
metrics.RecordLatency("latency_us", duration);
```

### Status Conversion

```cpp
#include "cpp/futility/status/status.h"

// In gRPC service implementation
grpc::Status MyService::DoWork(...) {
    absl::Status result = InternalLogic();
    return futility::status::AbseilToGrpc(result);
}
```

### Base64

```cpp
#include "cpp/futility/base64/base64.h"

std::vector<uint8_t> binary = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
std::string encoded = futility::base64::Base64::encode(binary);
std::vector<uint8_t> decoded = futility::base64::Base64::decode(encoded);
```

## Bazel Dependencies

```python
deps = [
    "//cpp/futility/rate_limiter:sliding_window_rate_limiter",
    "//cpp/futility/rate_limiter:token_bucket_rate_limiter",
    "//cpp/futility/cache:lru_cache",
    "//cpp/futility/otel:otel_provider",
    "//cpp/futility/otel:metrics",
    "//cpp/futility/status",
    "//cpp/futility/base64",
]
```

## Documentation

Each module has its own README with detailed API documentation:

- [Rate Limiter README](rate_limiter/README.md)
- [Cache README](cache/README.md)
- [OpenTelemetry README](otel/README.md)

Header files include Doxygen-style documentation for all public APIs.
