# Deployed Services Hardening Plan

This plan targets the services deployed in `deploy/consolidated/compose.yaml`, with the explicit assumption that all services except `mcpserver` are intended to be public and unauthenticated. The focus is on timeouts, limits, rate limiting/abuse protection, and improved testing/documentation.

## Scope

Services in the consolidated deploy:
- `games_ws_backend` (Go)
- `portrait` (C++)
- `prom_proxy` (Go)
- `mithril` (Rust)
- `posterize` (Rust)
- `r3dr` (Go)
- `mcpserver` (Java)

## Phase 1: Timeouts and Limits

### games_ws_backend (Go)
- Add HTTP server timeouts (`ReadTimeout`, `WriteTimeout`, `IdleTimeout`, `ReadHeaderTimeout`).
- Add WebSocket message rate limiting and per-connection limits.
- Add WS read/write deadlines tuned for production.
- Make allowed origins configurable (env/config), defaulting to current list.

### r3dr (Go)
- Replace `http.ListenAndServe` with configured `http.Server` timeouts.
- Enforce request body size limits for `/shorten`.
- Add global and per-IP limiter (reuse `resilience4g/rate_limit` after fixes).

### prom_proxy (Go)
- Add HTTP server timeouts.
- Add request size limits and per-endpoint rate limits.
- Enforce query range and step caps (protect Prometheus backend).

### portrait (C++)
- Add request size limits in `meerkat` server.
- Add request/response timeouts in `meerkat`.
- Make rate limiter configuration env-driven (max keys, window, ttl, cleanup).

### mithril (Rust)
- Adjust body limit for wordchain endpoints (lower than 4MB if feasible).
- Add rate limiting (per-IP or global) via `server_pal` or a service-level limiter.
- Cache/precompute word graph to reduce startup cost.

### posterize (Rust)
- Lower request body limit or add early base64 size validation.
- Add per-IP rate limiting.
- Add concurrency throttling (Tokio semaphore) for CPU-heavy image operations.

### mcpserver (Java/Micronaut)
- Configure `micronaut.server.idle-timeout` and `micronaut.server.read-timeout`.
- Implement `RequestSizeLimit` for file uploads or large POST bodies.
- Wire in `micronaut-ratelimit` or custom `Filter` for per-IP limiting.
- Ensure thread pool limits are tuned for the consolidated deployment resources.

## Phase 1b: Rate Limiting and Abuse Protection Libraries

### Go `resilience4g/rate_limit`
- **Fix Memory Leak**: The current `RateLimiterMiddleware` stores limiters in an unbounded map.
- Add `last_access` timestamps to tracked keys.
- Implement a cleanup mechanism (e.g., a background goroutine or lazy eviction during `Wrap`) that removes keys older than a configurable TTL.
- Handle empty/invalid keys (explicit 400 or safe fallback).
- Parse `X-Forwarded-For` chain with trusted proxy depth.
- Add `Retry-After` headers and configurable burst handling.

### C++ `domains/platform/libs/futility/rate_limiter`
- Make key limits, cleanup interval, and TTL configurable via env or config file.
- Add tests for eviction, cleanup behavior, and high-key volumes.

### Rust `server_pal`
- Add optional rate limiting layer with config hooks.
- Provide env-configurable body size defaults per service.

## Phase 2: Testing Improvements

### mcpserver
- Integration tests for Micronaut filter-based rate limiting.
- Stress tests for concurrent MCP connections.

### games_ws_backend
- Hub tests for origin validation and WS size limits.
- Integration tests for rate limiting and abusive patterns.

### r3dr
- Rate limiting eviction tests.
- Integration tests for body size limits and invalid inputs.

### prom_proxy
- Tests for query caps (range/step validation).
- Integration tests for Prometheus error handling and timeouts.

### portrait
- Tests for request size enforcement and rate limiting behavior.
- Basic concurrency/load test to establish baseline latency.

### mithril / posterize
- Validation tests for body limits and timeouts.
- Fuzz/property tests for image parsing (posterize).

## Phase 3: Documentation

For each deployed service:
- **Operational README**: ports, env vars, limits, rate limits.
- **SLO/SLA notes**: latency targets, expected QPS, error budgets.
- **Abuse model**: what’s mitigated and what is not.
- **Runbook**: restart, config update, observability locations.

## Suggested Execution Order

1. Add timeouts and size limits in Go services.
2. Fix Go rate limiting library memory leak and wire into services.
3. Configure Micronaut limits and timeouts for `mcpserver`.
4. Add request limits/timeouts in C++ `meerkat`.
5. Add rate limiting in Rust `server_pal` and services.
6. Expand tests and documentation across services.
