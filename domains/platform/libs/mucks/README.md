# Mucks

A lightweight HTTP router wrapper for Go (1.22+) that adds middleware support to the standard library's `http.ServeMux`.

## Features

- ✅ **Middleware chain**: Composable middleware pattern
- ✅ **Built-in JSON helpers**: `JsonOk()` and `JsonError()` utilities
- ✅ **Minimal dependencies**: Wraps Go's stdlib ServeMux
- ✅ **Simple API**: Easy to learn and integrate
- ✅ **Type-safe**: Leverages Go's type system

## Installation

```bash
# In your BUILD.bazel
deps = [
    "//domains/platform/libs/mucks",
]
```

```go
// In your Go code
import "github.com/muchq/moonbase/domains/platform/libs/mucks"
```

## Quick Start

### Basic HTTP Server

```go
package main

import (
    "net/http"
    "github.com/muchq/moonbase/domains/platform/libs/mucks"
)

func main() {
    // Create a new mucks router with JSON content-type middleware
    m := mucks.NewJsonMucks()

    // Register routes
    m.HandleFunc("GET /health", healthHandler)
    m.HandleFunc("POST /api/users", createUserHandler)

    // Start server
    http.ListenAndServe(":8080", m)
}

func healthHandler(w http.ResponseWriter, r *http.Request) {
    mucks.JsonOk(w, map[string]string{"status": "healthy"})
}

func createUserHandler(w http.ResponseWriter, r *http.Request) {
    // Your handler logic here
    mucks.JsonOk(w, map[string]string{"id": "123"})
}
```

### Using Middleware

Middleware wraps handlers to add cross-cutting concerns like logging, authentication, or rate limiting:

```go
// Create router without default middleware
m := mucks.NewMucks()

// Add custom middleware in order
m.Add(NewLoggingMiddleware())
m.Add(NewAuthMiddleware())
m.Add(NewJsonContentTypeMiddleware())

// Register routes - all middleware applies
m.HandleFunc("GET /api/data", dataHandler)
```

## Middleware Pattern

Implement the `Middleware` interface:

```go
type Middleware interface {
    Wrap(handlerFunc http.HandlerFunc) http.HandlerFunc
}
```

### Example: Logging Middleware

```go
type LoggingMiddleware struct {
    logger *slog.Logger
}

func (m *LoggingMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
    return func(w http.ResponseWriter, r *http.Request) {
        start := time.Now()

        m.logger.Info("Request started",
            "method", r.Method,
            "path", r.URL.Path,
            "remote", r.RemoteAddr)

        next(w, r)

        m.logger.Info("Request completed",
            "duration_ms", time.Since(start).Milliseconds())
    }
}
```

### Example: Rate Limiting Middleware

```go
import "github.com/muchq/moonbase/domains/platform/libs/resilience4g/rate_limit"

// Create rate limiter middleware
config := rate_limit.RateLimiterConfig{
    MaxTokens:  100,
    RefillRate: 100,
    OpCost:     1,
}

rateLimiter := rate_limit.NewRateLimiterMiddleware(
    rate_limit.TokenBucketRateLimiterFactory{},
    rate_limit.IpKeyExtractor{},
    config,
)

// Add to mucks
m := mucks.NewMucks()
m.Add(rateLimiter)
```

## JSON Helpers

### JsonOk - Success Response

```go
func JsonOk(w http.ResponseWriter, response any)
```

Sends a JSON-encoded response with status 200:

```go
type User struct {
    ID   string `json:"id"`
    Name string `json:"name"`
}

func getUser(w http.ResponseWriter, r *http.Request) {
    user := User{ID: "123", Name: "Alice"}
    mucks.JsonOk(w, user)
    // Response: {"id":"123","name":"Alice"}
}
```

### JsonError - Error Response

```go
func JsonError(w http.ResponseWriter, problem Problem)
```

Sends a JSON-encoded error with appropriate status code:

```go
func validateRequest(w http.ResponseWriter, r *http.Request) {
    if invalidInput {
        mucks.JsonError(w, mucks.NewBadRequest("Invalid email format"))
        return
    }
    // ... continue processing
}
```

Built-in error constructors:
- `NewBadRequest(detail string)` - 400
- `NewUnauthorized(detail string)` - 401
- `NewForbidden(detail string)` - 403
- `NewNotFound()` - 404
- `NewInternalError(detail string)` - 500

## Production Use Cases

### 1. Machine Learning Inference API

Used in [`mnist_production.go`](../../../../domains/ai/libs/neuro/examples/mnist_production.go):

```go
m := mucks.NewJsonMucks()
m.Add(rateLimiterMiddleware)
m.Add(loggingMiddleware)

m.HandleFunc("GET /health", healthHandler)
m.HandleFunc("POST /predict", predictHandler)

http.ListenAndServe(":8080", m)
```

### 2. URL Shortener Service

Used in [`r3dr`](../../../../domains/r3dr/apis/r3dr/):

```go
m := mucks.NewJsonMucks()
m.Add(ipRateLimiter)      // Per-IP rate limiting
m.Add(fallbackLimiter)    // Global rate limiting
m.Add(tracingMiddleware)  // Distributed tracing

m.HandleFunc("POST /shorten", shortenHandler)
m.HandleFunc("GET /{code}", redirectHandler)
```

### 3. Metrics Proxy

Used in [`prom_proxy`](../../apis/prom_proxy/):

```go
m := mucks.NewJsonMucks()

m.HandleFunc("GET /api/query", queryHandler)
m.HandleFunc("GET /api/query_range", queryRangeHandler)
m.HandleFunc("GET /api/metrics", metricsHandler)
```

## Middleware Execution Order

Middleware executes in the order it's added:

```go
m := mucks.NewMucks()
m.Add(middleware1)  // Runs first
m.Add(middleware2)  // Runs second
m.Add(middleware3)  // Runs third

// Request flow:
// Request → middleware1 → middleware2 → middleware3 → handler → middleware3 → middleware2 → middleware1 → Response
```

## Best Practices

1. **Use `NewJsonMucks()` for JSON APIs** - Sets content-type automatically
2. **Add rate limiting early** - Protect against abuse before expensive operations
3. **Log after rate limiting** - Don't log rate-limited requests
4. **Use structured logging** - Prefer `slog` over `fmt.Println`
5. **Return early on errors** - Use `JsonError()` and return immediately
6. **Keep middleware focused** - Each middleware should do one thing well

## Testing

```go
func TestHandler(t *testing.T) {
    m := mucks.NewJsonMucks()
    m.HandleFunc("GET /test", testHandler)

    req := httptest.NewRequest("GET", "/test", nil)
    w := httptest.NewRecorder()

    m.ServeHTTP(w, req)

    assert.Equal(t, 200, w.Code)
    assert.Contains(t, w.Body.String(), "expected")
}
```

## Related Libraries

- [`resilience4g`](../resilience4g/) - Rate limiting, circuit breakers, retry patterns
- [`server_pal`](../server_pal/) - Rust HTTP server utilities
- [`meerkat`](../meerkat/) - C++ HTTP server wrapper (Mongoose)

## License

Part of the MoonBase project.
