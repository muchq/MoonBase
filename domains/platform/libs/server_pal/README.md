# Server Pal

An Axum Router to merge into your API.

### Features:
 - request logging
 - request body size limiter (7MB default)
 - response body compression
 - validate request headers (application/json default)
 - request timeout handler (10s default)
 - panic -> error response handler (don't use it, but it's there)
 - a health check route at GET /health
