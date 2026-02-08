# Review Comments for PR #961

## Critical Issues

### 1. Use-After-Free in `SlidingWindowRateLimiter`
**File:** `domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h`

The `limiters_` map stores `std::unique_ptr<WindowState>`. The `get_or_create_state` method returns a raw pointer (`WindowState*`) to the stored object. However, `maybe_cleanup` (which can be triggered by concurrent calls to `allow` via `get_or_create_state`) removes entries from `limiters_`, destroying the `WindowState` object.

If Thread A calls `get_or_create_state` and receives a raw pointer, but is preempted before locking `state.mutex`, and Thread B calls `allow` triggering cleanup that evicts Thread A's key, the `WindowState` object is deleted. When Thread A resumes, it attempts to lock a mutex on a deleted object, causing a use-after-free crash.

**Suggestion:**
Change `limiters_` to store `std::shared_ptr<WindowState>` and return `std::shared_ptr<WindowState>` from `get_or_create_state`. This ensures the object remains alive as long as Thread A holds the pointer.

```cpp
// In SlidingWindowRateLimiter class
std::unordered_map<Key, std::shared_ptr<WindowState>> limiters_;

std::shared_ptr<WindowState> get_or_create_state(const Key& key) {
  // ...
  return limiters_.at(key); // returns shared_ptr
}
```

## Major Issues

### 2. Race Condition in `HttpServer` Destructor
**File:** `domains/platform/libs/meerkat/meerkat.cc`

The `~HttpServer` destructor calls `stop()`, which sets `running_ = false`. However, if `run()` is executing in another thread, it might be blocked in `poll()` or accessing member variables (`mgr_`, `listener_`) after the object is partially destroyed. Mongoose cleanup (`mg_mgr_free`) happens in `run()`, but `~HttpServer` might complete before `run()` finishes, leading to undefined behavior.

**Suggestion:**
Ensure strict lifecycle management. The `run()` method must return before `HttpServer` is destroyed. Consider adding an assertion or abort if `running_` is true in the destructor to enforce correct usage (joining the thread before destruction).

### 3. Missing URL Decoding in Query Parameters
**File:** `domains/platform/libs/meerkat/meerkat.cc`

The `parse_query_params` method splits the query string by `&` and `=`, but it does not URL-decode the keys or values. Parameters like `foo=bar%20baz` will be stored as `bar%20baz` instead of `bar baz`.

**Suggestion:**
Use `mg_url_decode` or a similar utility to decode keys and values after splitting.

## Minor Issues

### 4. Invalid IPv6 URL Construction
**File:** `domains/platform/libs/meerkat/meerkat.cc`

In `HttpServer::run`, the listening URL is constructed as:
```cpp
std::string url = "http://" + listen_address_ + ":" + std::to_string(listen_port_);
```
If `listen_address_` is an IPv6 address (e.g., `::1`), the resulting URL `http://::1:8080` is invalid (ambiguous colons).

**Suggestion:**
Detect IPv6 addresses and enclose them in brackets (e.g., `http://[::1]:8080`), or use a safer URL construction method.

### 5. Confusing `time.Duration` Usage in Go
**File:** `domains/r3dr/apis/r3dr/shortener.go`, `domains/r3dr/apis/r3dr/main.go`

`ShortenerCacheConfig.ExpirationMinutes` is typed as `time.Duration` but initialized with `5` (interpreted as 5 nanoseconds). In `main.go`, it is multiplied by `time.Minute`:
```go
time.Minute * cacheConfig.ExpirationMinutes
```
This relies on `time.Duration` being an integer type (`int64`). While `5ns * 60*10^9ns` mathematically results in the correct magnitude for 5 minutes, it effectively treats `ExpirationMinutes` as a dimensionless integer multiplier, despite being typed as `Duration`. This is confusing and fragile.

**Suggestion:**
Change `ExpirationMinutes` to `int`, or rename it to `Expiration` (type `time.Duration`) and initialize it with `5 * time.Minute`.

### 6. Single-Threaded Blocking Server
**File:** `domains/platform/libs/meerkat/meerkat.cc`

The `HttpServer` uses a single-threaded event loop (`poll` + synchronous handler). Any blocking operation in a handler will block the entire server. This limitation should be clearly documented or addressed if high concurrency is expected.
