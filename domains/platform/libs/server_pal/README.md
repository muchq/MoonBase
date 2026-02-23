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
- `GET /health` endpoint

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

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8080` | Port to listen on (used by `listen_addr_pal()`) |
