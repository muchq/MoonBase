# Rate Limiter

Thread-safe rate limiting implementations for controlling request rates in C++ services.

## Overview

This module provides three rate limiting implementations:

| Implementation | Use Case | Per-Key | Thread-Safe | Clock |
|----------------|----------|---------|-------------|-------|
| **Sliding Window** | Per-client/IP limiting | Yes | Yes | Internal |
| **Token Bucket (global)** | Global rate limiting | No | Yes | Internal |
| **TokenBucket (single-owner)** | One budget owned by one execution context | No | No — by design | Injected |

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

## Single-Owner TokenBucket

`token_bucket.h` is the third shape: one bucket, no lock, and the caller
supplies every clock reading. It exists for budgets owned by exactly one
execution context — the motivating case is golf_hub's per-session stream
budgets (MoonBase#1240), which live in the session's coroutine frame where
frames are handled sequentially and a mutex or clock call per admit would
be pure overhead. Injected time is also what makes refill behavior
testable with fabricated clocks instead of sleeps.

### Usage

```cpp
#include "domains/platform/libs/futility/rate_limiter/token_bucket.h"

// Burst of 10, sustained 5/s. Starts full.
futility::rate_limiter::TokenBucket budget(10, 5);

// The owner reads the clock once and passes it in.
if (!budget.Admit(std::chrono::steady_clock::now())) {
    Reject("slow down");
}
```

Tokens are fractional, so slow refills (e.g. 0.5/s) grant eventually
rather than never; a clock reading at or before the last one refills
nothing and never moves the watermark backwards.

## Choosing an Algorithm

| Scenario | Recommended |
|----------|-------------|
| Rate limit by IP address | Sliding Window |
| Rate limit by API key | Sliding Window |
| Global service-wide limit | Token Bucket (global) |
| Need to allow temporary bursts | Either token bucket |
| Memory-constrained environment | Sliding Window with max_keys |
| One budget per session/connection, owner-confined | TokenBucket (single-owner) |

## Testing

The keyed and global limiters accept a Clock template parameter for
deterministic testing; the single-owner TokenBucket needs no mock at all —
its owner passes time in, so tests just fabricate readings:

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
deps = ["//domains/platform/libs/futility/rate_limiter:sliding_window_rate_limiter"]
deps = ["//domains/platform/libs/futility/rate_limiter:token_bucket_rate_limiter"]
deps = ["//domains/platform/libs/futility/rate_limiter:token_bucket"]
```
