# Platform Domain

Core infrastructure, shared services, and foundational libraries.

## APIs

- [**DocDB**](apis/doc_db): Document database system with implementations in Rust and Go.
- [**Prom Proxy**](apis/prom_proxy): Proxy service for Prometheus metrics.
- [**Example gRPC**](apis/example_grpc_go): Sample gRPC service in Go.

## Libraries

- [**Aura**](libs/aura): Serving chain for C++ smithy-cpp services (observability, health, rate limiting).
- [**Futility**](libs/futility): Collection of C++ utility libraries (Rate Limiter, Cache, Otel, etc.).
- [**Mucks**](libs/mucks): Lightweight HTTP router wrapper for Go with middleware support.
- [**Resilience4g**](libs/resilience4g): Fault tolerance library for Go (Rate Limiting, Circuit Breakers).
- [**Server Pal**](libs/server_pal): Rust HTTP server utilities.
- [**DocDB Client**](libs/doc_db_client_go): Client library for DocDB in Go.
- [**HTTP Client**](libs/http_client): Shared HTTP client utilities.
- [**Logging**](libs/logging): Common logging infrastructure.
- [**Clock**](libs/clock): Time and clock utilities for Go.
- [**JSON**](libs/json): JSON processing utilities.
