# Rate Limiter

Thread-safe rate limiting implementations for controlling request rates in C++ services.

## Overview

This module provides two rate limiting algorithms:

| Algorithm | Use Case | Per-Key | Burst Handling |
|-----------|----------|---------|----------------|
| **Sliding Window** | Per-client/IP limiting | Yes | Smooth (no boundary bursts) |
| **Token Bucket** | Global rate limiting | No | Allows controlled bursts |

## Sliding Window Rate Limiter

The sliding window algorithm provides smooth rate limiting without the "burst at boundary" problem common in fixed-window approaches. It interpolates between two consecutive time windows.

### Features

- **Per-key limiting**: Track separate limits for each client (IP, user ID, API key, etc.)
- **Thread-safe**: Uses shared mutex for the key map and per-key mutexes for state
- **Automatic cleanup**: Evicts inactive keys based on TTL to prevent memory growth
- **Configurable max keys**: Protect against key cardinality attacks
- **Variable cost**: Support for requests that consume more than one unit of quota

### Usage

```cpp
#include "cpp/futility/rate_limiter/sliding_window_rate_limiter.h"

using namespace futility::rate_limiter;

// Configure: 100 requests per minute per key
SlidingWindowRateLimiterConfig config{
    .max_requests_per_key = 100,
    .window_size = std::chrono::minutes(1),
    .ttl = std::chrono::minutes(5),           // Evict keys after 5 min idle
    .cleanup_interval = std::chrono::seconds(30),
    .max_keys = 10000                          // Limit tracked keys
};

SlidingWindowRateLimiter<std::string> limiter(config);

// In request handler
std::string client_ip = GetClientIP(request);
if (!limiter.allow(client_ip)) {
    return HttpResponse(429, "Too Many Requests");
}
// Process request...
```

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `max_requests_per_key` | Max requests allowed per key in window | (required) |
| `window_size` | Duration of the sliding window | (required) |
| `ttl` | Time before inactive keys are evicted | 5 minutes |
| `cleanup_interval` | How often cleanup runs | 30 seconds |
| `max_keys` | Maximum unique keys to track | unlimited |

### Variable Cost Requests

For batch operations or requests that should consume more quota:

```cpp
// Batch request consuming 10 units
if (limiter.allow(client_ip, 10)) {
    ProcessBatch(request);
}
```

## Token Bucket Rate Limiter

The token bucket algorithm allows controlled bursts while enforcing a sustained rate limit. Tokens accumulate over time up to a maximum, enabling burst handling.

### Features

- **Global limiting**: Single bucket for all requests (not per-key)
- **Burst support**: Allows bursts up to the bucket capacity
- **Thread-safe**: Mutex-protected access

### Usage

```cpp
#include "cpp/futility/rate_limiter/token_bucket_rate_limiter.h"

using namespace futility::rate_limiter;

// Configure: 100 tokens/sec refill, max 500 tokens (burst capacity)
TokenBucketConfig config{
    .refill_rate_seconds = 100,
    .max_tokens = 500
};

TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

if (!limiter.allow(1)) {
    return HttpResponse(429, "Too Many Requests");
}
```

## Choosing an Algorithm

| Scenario | Recommended |
|----------|-------------|
| Rate limit by IP address | Sliding Window |
| Rate limit by API key | Sliding Window |
| Global service-wide limit | Token Bucket |
| Need to allow temporary bursts | Token Bucket |
| Memory-constrained environment | Sliding Window with max_keys |

## Testing

Both limiters accept a Clock template parameter for deterministic testing:

```cpp
class MockClock {
public:
    using duration = std::chrono::steady_clock::duration;
    using time_point = std::chrono::steady_clock::time_point;
    static constexpr bool is_steady = true;

    static time_point now() { return current_time_; }
    static void advance(duration d) { current_time_ += d; }
private:
    static time_point current_time_;
};

// In tests
SlidingWindowRateLimiter<std::string, MockClock> limiter(config);
MockClock::advance(std::chrono::seconds(60));  // Simulate time passing
```

## Bazel Targets

```python
deps = ["//cpp/futility/rate_limiter:sliding_window_rate_limiter"]
deps = ["//cpp/futility/rate_limiter:token_bucket_rate_limiter"]
```
