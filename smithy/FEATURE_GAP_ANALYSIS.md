# Smithy Server Generator Feature Gap Analysis

This document compares the features produced by the Smithy server generator against the actual patterns and features currently in use in deployed MoonBase applications across all domains.

## Executive Summary

The Smithy generator provides basic code generation for HTTP REST and WebSocket services across Java, Go, Rust, and C++. However, significant feature gaps exist between what's generated and what's needed for production-grade services currently deployed in MoonBase.

### Priority Gap Areas

| Priority | Gap Area | Impact |
|----------|----------|--------|
| **P0** | Middleware/Interceptor Chain | No request/response interceptors, authentication, rate limiting |
| **P0** | Observability (Metrics/Tracing) | No OpenTelemetry integration, no Prometheus metrics |
| **P0** | gRPC Support | No Protocol Buffers or gRPC code generation |
| **P1** | Input Validation | No field validation beyond required checks |
| **P1** | Error Handling Patterns | No RFC 7807 Problem Details, no structured errors |
| **P1** | Configuration Management | No environment/file config loading patterns |
| **P2** | Caching | No LRU cache or response caching |
| **P2** | Health Checks | No standardized health endpoints |
| **P2** | Connection Pooling | No database connection management |

---

## Detailed Analysis by Language

### Java

#### Currently in MoonBase

| Feature | Implementation | Location |
|---------|---------------|----------|
| TCP Server | Netty NioEventLoopGroup | `domains/chat/libs/yochat_lib/YoServer.java` |
| HTTP Client | JDK 11 HttpClient | `domains/platform/libs/http_client/` |
| JSON | Jackson | `domains/platform/libs/json/JsonUtils.java` |
| Nullability | JSpecify annotations | Used throughout |

#### Smithy Generator Produces
- Service interfaces with `CompletableFuture<T>` async
- Builder pattern for structures
- Enum with `fromValue()` method
- Sealed classes for unions
- HTTP router with regex patterns
- WebSocket handler with session management

#### Gaps

1. **No Netty Integration**
   - MoonBase uses Netty for high-performance TCP
   - Smithy generates generic `Router` interface without server implementation

2. **No DI Framework Support**
   - No Guice, Spring, or Dagger integration
   - No factory/provider patterns

3. **Missing JSON Configuration**
   - No Jackson annotations on generated classes
   - No custom serializers for Instant/Timestamp

---

### Go

#### Currently in MoonBase

| Feature | Implementation | Location |
|---------|---------------|----------|
| HTTP Router | Custom `mucks` + net/http | `domains/platform/libs/mucks/` |
| gRPC | google.golang.org/grpc | `domains/platform/apis/doc_db_go/` |
| WebSocket | gorilla/websocket | `domains/games/apis/games_ws_backend/hub/` |
| Middleware | Chain of Wrap functions | `domains/platform/libs/mucks/middleware.go` |
| Rate Limiting | Token bucket | `domains/platform/libs/resilience4g/` |
| Error Handling | RFC 7807 Problem | `domains/platform/libs/mucks/problem.go` |
| Logging | slog (structured) | Throughout |
| Validation | Multi-level validators | `domains/games/apis/games_ws_backend/golf/state_validation.go` |
| Caching | HashiCorp LRU | `domains/r3dr/apis/r3dr/main.go` |
| Config | Env + file fallback | `domains/r3dr/apis/r3dr/config.go` |

#### Smithy Generator Produces
- net/http handlers
- gorilla/websocket integration
- Basic JSON encoding/decoding
- Hub pattern for WebSocket broadcast

#### Gaps

1. **No Middleware Chain**
   ```go
   // MoonBase pattern (not in Smithy):
   type Middleware interface {
       Wrap(next http.HandlerFunc) http.HandlerFunc
   }

   // Applied as:
   router.Add(JsonContentTypeMiddleware{})
   router.Add(RateLimiterMiddleware{...})
   ```

2. **No gRPC Support**
   - MoonBase has extensive gRPC usage (`doc_db_go`, `example_grpc_go`)
   - Smithy only generates HTTP/WebSocket

3. **No RFC 7807 Problem Details**
   ```go
   // MoonBase has:
   type Problem struct {
       StatusCode int    `json:"status"`
       ErrorCode  int    `json:"errorCode"`
       Message    string `json:"message"`
       Detail     string `json:"detail"`
       Instance   string `json:"instance"` // UUID for tracking
   }
   ```

4. **No Rate Limiting Integration**
   ```go
   // MoonBase has per-key rate limiters:
   type RateLimiterMiddleware struct {
       Factory   RateLimiterFactory
       Limiters  map[string]RateLimiter
       Extractor KeyExtractor  // IP-based or global
       Config    RateLimiterConfig
   }
   ```

5. **No Structured Logging**
   - No slog integration
   - No structured log fields

6. **No Multi-Level Validation**
   ```go
   // MoonBase has state validators:
   type StateValidator struct {
       mu sync.RWMutex
   }

   func (sv *StateValidator) ValidateGameInvariants(game *Game) error
   func ValidatePosition(pos Position) error
   func ValidateColor(color Color) error
   ```

---

### Rust

#### Currently in MoonBase

| Feature | Implementation | Location |
|---------|---------------|----------|
| HTTP Server | Axum | `domains/graphics/apis/posterize/` |
| gRPC | Tonic | `domains/platform/apis/doc_db/` |
| Async Runtime | Tokio | Throughout |
| Error Handling | Status::invalid_argument | `domains/platform/apis/doc_db/doc_db.rs` |
| Validation | Field validators | `domains/platform/apis/doc_db/doc_db.rs:45-88` |
| Database | MongoDB driver | `domains/platform/apis/doc_db/` |

#### Smithy Generator Produces
- Axum handlers
- Tokio async_trait
- Serde derive macros
- WebSocket with broadcast channels

#### Gaps

1. **No Tonic/gRPC Support**
   - MoonBase's `doc_db` uses Tonic for gRPC
   - Smithy only generates HTTP/WebSocket

2. **No Database Patterns**
   ```rust
   // MoonBase has:
   trait Crud {
       async fn insert_one(&self, db: String, collection: String, doc: MongoDocEgg) -> Result<ObjectId, MongoError>;
       async fn find_one(&self, db: String, collection: String, query: BsonDocument) -> MongoResult<Option<MongoDoc>>;
   }
   ```

3. **No gRPC Metadata Reading**
   ```rust
   // MoonBase reads db_namespace from metadata:
   fn read_db_name_from_metadata(metadata: &MetadataMap) -> Option<String>
   ```

4. **No Tower Middleware**
   - No tower layers for tracing, auth, rate limiting

---

### C++

#### Currently in MoonBase

| Feature | Implementation | Location |
|---------|---------------|----------|
| HTTP Server | Mongoose (meerkat) | `domains/platform/libs/meerkat/` |
| gRPC | grpc++ | `domains/platform/apis/example_grpc_cpp/` |
| Interceptors | Request/Response chains | `domains/platform/libs/meerkat/meerkat.cc` |
| Rate Limiting | Sliding window | `domains/platform/libs/futility/rate_limiter/` |
| Metrics | OpenTelemetry | `domains/platform/libs/futility/otel/metrics.cc` |
| Caching | LRU Cache | `domains/platform/libs/futility/cache/` |
| Status/Errors | absl::StatusOr | `domains/graphics/apis/portrait/tracer_service.cc` |
| Tracing | x-trace-id header | `domains/platform/libs/meerkat/meerkat.cc:158-161` |
| Health Checks | `/health` endpoint | `domains/platform/libs/meerkat/meerkat.cc:152-156` |
| Logging | absl::log | Throughout |

#### Smithy Generator Produces
- nlohmann/json structures
- Basic HTTP handler interface
- WebSocket handler header/source

#### Gaps

1. **No Mongoose/Meerkat Integration**
   ```cpp
   // MoonBase meerkat has:
   class HttpServer {
       void get(const std::string& path, RouteHandler handler);
       void post(const std::string& path, RouteHandler handler);
       void use_request_interceptor(RequestInterceptor interceptor);
       void use_response_interceptor(ResponseInterceptor interceptor);
       void enable_health_checks();
       void enable_tracing();
       void enable_metrics(const std::string& service_name);
   };
   ```

2. **No Interceptor Chain**
   ```cpp
   // MoonBase pattern:
   using RequestInterceptor = std::function<bool(HttpRequest&, HttpResponse&, Context&)>;
   using ResponseInterceptor = std::function<void(const HttpRequest&, HttpResponse&, Context&)>;
   ```

3. **No OpenTelemetry Metrics**
   ```cpp
   // MoonBase has:
   class MetricsRecorder {
       void RecordCounter(const std::string& metric_name, int64_t value,
                         const std::map<std::string, std::string>& attributes);
       void RecordLatency(const std::string& metric_name,
                         std::chrono::microseconds duration,
                         const std::map<std::string, std::string>& attributes);
       void RecordGauge(const std::string& metric_name, double value,
                       const std::map<std::string, std::string>& attributes);
   };
   ```

4. **No absl::StatusOr Error Pattern**
   ```cpp
   // MoonBase uses:
   absl::StatusOr<TraceResponse> trace(TraceRequest& request);

   // With automatic status wrapping:
   namespace responses {
       HttpResponse wrap(const absl::StatusOr<json> status_or_data);
   }
   ```

5. **No gRPC Support**
   - MoonBase has `example_grpc_cpp` with full grpc++ integration

6. **No Rate Limiting**
   ```cpp
   // MoonBase has sliding window and token bucket:
   RequestInterceptor rate_limiter(std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter);
   ```

---

## Feature Gap Matrix

| Feature | Java | Go | Rust | C++ |
|---------|------|-----|------|-----|
| **HTTP REST** | Basic | Basic | Basic | Basic |
| **WebSocket** | Yes | Yes | Yes | Yes |
| **gRPC** | No | No | No | No |
| **Middleware Chain** | No | No | No | No |
| **Request Interceptors** | No | No | No | No |
| **Response Interceptors** | No | No | No | No |
| **Authentication** | No | No | No | No |
| **Rate Limiting** | No | No | No | No |
| **OpenTelemetry Metrics** | No | No | No | No |
| **Distributed Tracing** | No | No | No | No |
| **Health Checks** | No | No | No | No |
| **Structured Logging** | No | No | No | No |
| **RFC 7807 Errors** | No | No | No | No |
| **absl::Status Pattern** | N/A | N/A | No | No |
| **Input Validation** | Required only | Required only | Required only | Required only |
| **Field Range Validation** | No | No | No | No |
| **Configuration Loading** | No | No | No | No |
| **LRU Caching** | No | No | No | No |
| **Connection Pooling** | No | No | No | No |
| **Builder Pattern** | Yes | No | Yes | No |
| **Async/Futures** | Yes | Yes | Yes | No |

---

## Recommendations

### Phase 1: Foundation (Critical)

1. **Add Middleware/Interceptor Support**
   - Generate middleware interface per language
   - Support request/response interceptor chains
   - Integrate with existing MoonBase middleware patterns

2. **Add gRPC Code Generation**
   - Generate `.proto` files from Smithy models
   - Generate gRPC server/client stubs for all languages
   - Support streaming operations

3. **Add Observability**
   - Generate OpenTelemetry instrumentation
   - Add Prometheus metrics endpoints
   - Support distributed tracing (x-trace-id propagation)

### Phase 2: Production Readiness

4. **Add Structured Error Handling**
   - RFC 7807 Problem Details for HTTP
   - absl::StatusOr pattern for C++
   - Status codes for gRPC

5. **Add Configuration Management**
   - Environment variable loading
   - File-based config fallback
   - Config validation

6. **Add Input Validation**
   - Field-level validators
   - Range/pattern constraints from Smithy traits
   - Custom validation hooks

### Phase 3: Performance & Operations

7. **Add Caching**
   - LRU cache integration
   - Cache configuration from traits

8. **Add Health Checks**
   - `/health` and `/ready` endpoints
   - Dependency health aggregation

9. **Add Rate Limiting**
   - Per-endpoint rate limiting
   - Token bucket and sliding window algorithms

---

## Migration Path

For services currently using the patterns documented above, the Smithy generator should eventually produce code that integrates with:

| Language | Target Framework | Integration Point |
|----------|-----------------|-------------------|
| Java | Netty | Generate Netty handlers |
| Go | mucks | Generate middleware-compatible handlers |
| Rust | Tonic + Axum | Generate both gRPC and HTTP |
| C++ | meerkat | Generate meerkat-compatible handlers |

---

## Appendix: File References

### MoonBase Patterns

- Go HTTP Router: `domains/platform/libs/mucks/mucks.go`
- Go Middleware: `domains/platform/libs/mucks/middleware.go`
- Go RFC 7807: `domains/platform/libs/mucks/problem.go`
- Go Rate Limiting: `domains/platform/libs/resilience4g/rate_limit/`
- Go WebSocket: `domains/games/apis/games_ws_backend/hub/hub.go`
- Go gRPC: `domains/platform/apis/doc_db_go/`
- Rust gRPC: `domains/platform/apis/doc_db/doc_db.rs`
- Rust HTTP: `domains/graphics/apis/posterize/service.rs`
- C++ HTTP: `domains/platform/libs/meerkat/meerkat.cc`
- C++ Metrics: `domains/platform/libs/futility/otel/metrics.cc`
- C++ gRPC: `domains/platform/apis/example_grpc_cpp/`
- Java Netty: `domains/chat/libs/yochat_lib/YoServer.java`

### Smithy Generator

- Java Generator: `smithy/generators/java/src/main/java/com/moonbase/smithy/java/JavaServerGenerator.java`
- Go Generator: `smithy/generators/go/src/main/java/com/moonbase/smithy/go/GoServerGenerator.java`
- Rust Generator: `smithy/generators/rust/src/main/java/com/moonbase/smithy/rust/RustServerGenerator.java`
- C++ Generator: `smithy/generators/cpp/src/main/java/com/moonbase/smithy/cpp/CppServerGenerator.java`
