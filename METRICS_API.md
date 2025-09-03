# Metrics API Service

A Go microservice that queries Prometheus for system and application metrics and exposes them via REST API.

## Prometheus Queries

### System Metrics

#### CPU Usage
```bash
# Current CPU utilization by core
curl "http://localhost:9090/api/v1/query?query=rate(system_cpu_time_seconds_total%5B5m%5D)*100"

# Average CPU usage across all cores
curl "http://localhost:9090/api/v1/query?query=100-avg(rate(system_cpu_time_seconds_total{state=\"idle\"}%5B5m%5D))*100"
```

#### Memory Metrics
```bash
# Memory usage breakdown
curl "http://localhost:9090/api/v1/query?query=system_memory_usage_bytes"

# Memory utilization percentage
curl "http://localhost:9090/api/v1/query?query=system_memory_usage_bytes{state=\"used\"}/on()group_left()(sum(system_memory_usage_bytes))*100"

# Available memory
curl "http://localhost:9090/api/v1/query?query=system_memory_usage_bytes{state=\"free\"}%2Bsystem_memory_usage_bytes{state=\"cached\"}"
```

#### Disk Metrics
```bash
# Disk usage by filesystem
curl "http://localhost:9090/api/v1/query?query=system_filesystem_usage_bytes"

# Disk I/O rates
curl "http://localhost:9090/api/v1/query?query=rate(system_disk_io_bytes_total%5B5m%5D)"

# Disk operations per second
curl "http://localhost:9090/api/v1/query?query=rate(system_disk_operations_total%5B5m%5D)"
```

#### Network Metrics
```bash
# Network I/O rates
curl "http://localhost:9090/api/v1/query?query=rate(system_network_io_bytes_total%5B5m%5D)"

# Network packets per second
curl "http://localhost:9090/api/v1/query?query=rate(system_network_packets_total%5B5m%5D)"

# Network errors
curl "http://localhost:9090/api/v1/query?query=rate(system_network_errors_total%5B5m%5D)"
```

### Portrait Application Metrics

#### Request Metrics
```bash
# Total requests
curl "http://localhost:9090/api/v1/query?query=trace_requests_total"

# Request rate (requests per second)
curl "http://localhost:9090/api/v1/query?query=rate(trace_requests_total%5B5m%5D)"

# Request success rate
curl "http://localhost:9090/api/v1/query?query=rate(trace_requests_completed_total%5B5m%5D)/rate(trace_requests_total%5B5m%5D)*100"

# Average request duration
curl "http://localhost:9090/api/v1/query?query=rate(trace_request_duration_microseconds_sum%5B5m%5D)/rate(trace_request_duration_microseconds_count%5B5m%5D)"
```

#### Cache Metrics
```bash
# Cache hit rate
curl "http://localhost:9090/api/v1/query?query=rate(trace_cache_hits_total%5B5m%5D)/(rate(trace_cache_hits_total%5B5m%5D)%2Brate(trace_cache_misses_total%5B5m%5D))*100"

# Cache operations per second
curl "http://localhost:9090/api/v1/query?query=rate(trace_cache_hits_total%5B5m%5D)%2Brate(trace_cache_misses_total%5B5m%5D)"
```

#### Scene Complexity
```bash
# Current scene complexity
curl "http://localhost:9090/api/v1/query?query=scene_sphere_count_gauge"
curl "http://localhost:9090/api/v1/query?query=scene_light_count_gauge"

# Average scene complexity over time
curl "http://localhost:9090/api/v1/query?query=avg_over_time(scene_sphere_count_gauge%5B1h%5D)"
```

## Go Microservice Design

### Project Structure
```
metrics-api/
├── cmd/
│   └── server/
│       └── main.go
├── internal/
│   ├── config/
│   │   └── config.go
│   ├── handlers/
│   │   ├── health.go
│   │   ├── metrics.go
│   │   └── system.go
│   ├── prometheus/
│   │   ├── client.go
│   │   └── queries.go
│   ├── models/
│   │   └── metrics.go
│   └── server/
│       └── server.go
├── pkg/
│   └── response/
│       └── response.go
├── tests/
│   ├── integration/
│   └── unit/
├── docker/
│   └── Dockerfile
├── go.mod
├── go.sum
└── README.md
```

### Recommended Libraries

```go
// go.mod
module metrics-api

go 1.21

require (
    github.com/gin-gonic/gin v1.9.1              // HTTP framework
    github.com/prometheus/client_golang v1.17.0  // Prometheus Go client
    github.com/spf13/viper v1.17.0               // Configuration
    github.com/stretchr/testify v1.8.4           // Testing framework
    go.uber.org/zap v1.26.0                      // Structured logging
    github.com/kelseyhightower/envconfig v1.4.0  // Environment config
)
```

### Core Implementation

#### Configuration (`internal/config/config.go`)
```go
package config

import (
    "github.com/kelseyhightower/envconfig"
)

type Config struct {
    Port           string `envconfig:"PORT" default:"8080"`
    PrometheusURL  string `envconfig:"PROMETHEUS_URL" default:"http://localhost:9090"`
    LogLevel       string `envconfig:"LOG_LEVEL" default:"info"`
    ReadTimeout    int    `envconfig:"READ_TIMEOUT" default:"30"`
    WriteTimeout   int    `envconfig:"WRITE_TIMEOUT" default:"30"`
}

func Load() (*Config, error) {
    var cfg Config
    err := envconfig.Process("", &cfg)
    return &cfg, err
}
```

#### Prometheus Client (`internal/prometheus/client.go`)
```go
package prometheus

import (
    "context"
    "encoding/json"
    "fmt"
    "net/http"
    "net/url"
    "time"
    
    "go.uber.org/zap"
)

type Client struct {
    baseURL    string
    httpClient *http.Client
    logger     *zap.Logger
}

type QueryResponse struct {
    Status string `json:"status"`
    Data   struct {
        ResultType string   `json:"resultType"`
        Result     []Result `json:"result"`
    } `json:"data"`
}

type Result struct {
    Metric map[string]string `json:"metric"`
    Value  []interface{}     `json:"value"`
}

func NewClient(baseURL string, logger *zap.Logger) *Client {
    return &Client{
        baseURL: baseURL,
        httpClient: &http.Client{
            Timeout: 30 * time.Second,
        },
        logger: logger,
    }
}

func (c *Client) Query(ctx context.Context, query string) (*QueryResponse, error) {
    u, err := url.Parse(fmt.Sprintf("%s/api/v1/query", c.baseURL))
    if err != nil {
        return nil, err
    }
    
    params := url.Values{}
    params.Add("query", query)
    u.RawQuery = params.Encode()
    
    req, err := http.NewRequestWithContext(ctx, "GET", u.String(), nil)
    if err != nil {
        return nil, err
    }
    
    resp, err := c.httpClient.Do(req)
    if err != nil {
        return nil, err
    }
    defer resp.Body.Close()
    
    var result QueryResponse
    if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
        return nil, err
    }
    
    return &result, nil
}
```

#### Models (`internal/models/metrics.go`)
```go
package models

import "time"

type SystemMetrics struct {
    Timestamp time.Time `json:"timestamp"`
    CPU       CPUMetrics `json:"cpu"`
    Memory    MemoryMetrics `json:"memory"`
    Disk      []DiskMetrics `json:"disk"`
    Network   []NetworkMetrics `json:"network"`
}

type CPUMetrics struct {
    Utilization float64            `json:"utilization_percent"`
    ByCore      map[string]float64 `json:"by_core"`
}

type MemoryMetrics struct {
    Total       float64 `json:"total_bytes"`
    Used        float64 `json:"used_bytes"`
    Free        float64 `json:"free_bytes"`
    Cached      float64 `json:"cached_bytes"`
    Utilization float64 `json:"utilization_percent"`
}

type DiskMetrics struct {
    Device      string  `json:"device"`
    Used        float64 `json:"used_bytes"`
    Total       float64 `json:"total_bytes"`
    Utilization float64 `json:"utilization_percent"`
    IORate      float64 `json:"io_rate_bytes_per_sec"`
}

type NetworkMetrics struct {
    Interface string  `json:"interface"`
    RxRate    float64 `json:"rx_rate_bytes_per_sec"`
    TxRate    float64 `json:"tx_rate_bytes_per_sec"`
    Errors    float64 `json:"errors_per_sec"`
}

type PortraitMetrics struct {
    Timestamp       time.Time     `json:"timestamp"`
    Requests        RequestMetrics `json:"requests"`
    Cache          CacheMetrics   `json:"cache"`
    SceneComplexity SceneMetrics  `json:"scene_complexity"`
}

type RequestMetrics struct {
    Total           float64 `json:"total"`
    Rate            float64 `json:"rate_per_sec"`
    SuccessRate     float64 `json:"success_rate_percent"`
    AverageDuration float64 `json:"avg_duration_microseconds"`
}

type CacheMetrics struct {
    HitRate         float64 `json:"hit_rate_percent"`
    OperationsRate  float64 `json:"operations_per_sec"`
}

type SceneMetrics struct {
    AverageSpheres float64 `json:"avg_spheres"`
    AverageLights  float64 `json:"avg_lights"`
}
```

#### Handlers (`internal/handlers/metrics.go`)
```go
package handlers

import (
    "context"
    "net/http"
    "time"
    
    "github.com/gin-gonic/gin"
    "go.uber.org/zap"
    
    "metrics-api/internal/models"
    "metrics-api/internal/prometheus"
)

type MetricsHandler struct {
    promClient *prometheus.Client
    logger     *zap.Logger
}

func NewMetricsHandler(promClient *prometheus.Client, logger *zap.Logger) *MetricsHandler {
    return &MetricsHandler{
        promClient: promClient,
        logger:     logger,
    }
}

func (h *MetricsHandler) GetSystemMetrics(c *gin.Context) {
    ctx, cancel := context.WithTimeout(c.Request.Context(), 10*time.Second)
    defer cancel()
    
    metrics, err := h.fetchSystemMetrics(ctx)
    if err != nil {
        h.logger.Error("Failed to fetch system metrics", zap.Error(err))
        c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to fetch metrics"})
        return
    }
    
    c.JSON(http.StatusOK, metrics)
}

func (h *MetricsHandler) GetPortraitMetrics(c *gin.Context) {
    ctx, cancel := context.WithTimeout(c.Request.Context(), 10*time.Second)
    defer cancel()
    
    metrics, err := h.fetchPortraitMetrics(ctx)
    if err != nil {
        h.logger.Error("Failed to fetch portrait metrics", zap.Error(err))
        c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to fetch metrics"})
        return
    }
    
    c.JSON(http.StatusOK, metrics)
}

func (h *MetricsHandler) fetchSystemMetrics(ctx context.Context) (*models.SystemMetrics, error) {
    // Implementation to query Prometheus and build SystemMetrics struct
    // This would make multiple queries and aggregate the results
    return &models.SystemMetrics{
        Timestamp: time.Now(),
        // ... populate fields from Prometheus queries
    }, nil
}

func (h *MetricsHandler) fetchPortraitMetrics(ctx context.Context) (*models.PortraitMetrics, error) {
    // Implementation to query Prometheus and build PortraitMetrics struct
    return &models.PortraitMetrics{
        Timestamp: time.Now(),
        // ... populate fields from Prometheus queries
    }, nil
}
```

### API Endpoints

```
GET /health                 - Health check
GET /api/v1/metrics/system  - System metrics (CPU, memory, disk, network)
GET /api/v1/metrics/portrait - Portrait application metrics
GET /api/v1/metrics/summary - Combined overview metrics
```

### Testing Strategy

#### Unit Tests
```go
// tests/unit/prometheus_test.go
package unit

import (
    "context"
    "net/http"
    "net/http/httptest"
    "testing"
    
    "github.com/stretchr/testify/assert"
    "go.uber.org/zap/zaptest"
    
    "metrics-api/internal/prometheus"
)

func TestPrometheusClient_Query(t *testing.T) {
    // Mock Prometheus server
    server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        response := `{
            "status": "success",
            "data": {
                "resultType": "vector",
                "result": [
                    {
                        "metric": {"__name__": "test_metric"},
                        "value": [1609459200, "42"]
                    }
                ]
            }
        }`
        w.Header().Set("Content-Type", "application/json")
        w.WriteHeader(http.StatusOK)
        w.Write([]byte(response))
    }))
    defer server.Close()
    
    logger := zaptest.NewLogger(t)
    client := prometheus.NewClient(server.URL, logger)
    
    result, err := client.Query(context.Background(), "test_metric")
    
    assert.NoError(t, err)
    assert.Equal(t, "success", result.Status)
    assert.Len(t, result.Data.Result, 1)
}
```

#### Integration Tests
```go
// tests/integration/api_test.go
package integration

import (
    "encoding/json"
    "net/http"
    "net/http/httptest"
    "testing"
    
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/suite"
    
    "metrics-api/internal/models"
)

type APITestSuite struct {
    suite.Suite
    server *httptest.Server
}

func (suite *APITestSuite) SetupSuite() {
    // Setup test server with real dependencies
}

func (suite *APITestSuite) TearDownSuite() {
    suite.server.Close()
}

func (suite *APITestSuite) TestGetSystemMetrics() {
    resp, err := http.Get(suite.server.URL + "/api/v1/metrics/system")
    assert.NoError(suite.T(), err)
    assert.Equal(suite.T(), http.StatusOK, resp.StatusCode)
    
    var metrics models.SystemMetrics
    err = json.NewDecoder(resp.Body).Decode(&metrics)
    assert.NoError(suite.T(), err)
    assert.NotZero(suite.T(), metrics.Timestamp)
}

func TestAPITestSuite(t *testing.T) {
    suite.Run(t, new(APITestSuite))
}
```

#### Load Tests
```go
// tests/load/load_test.go
package load

import (
    "net/http"
    "sync"
    "testing"
    "time"
    
    "github.com/stretchr/testify/assert"
)

func TestConcurrentRequests(t *testing.T) {
    const numRequests = 100
    const concurrency = 10
    
    var wg sync.WaitGroup
    results := make(chan int, numRequests)
    
    for i := 0; i < concurrency; i++ {
        wg.Add(1)
        go func() {
            defer wg.Done()
            for j := 0; j < numRequests/concurrency; j++ {
                resp, err := http.Get("http://localhost:8080/api/v1/metrics/system")
                assert.NoError(t, err)
                results <- resp.StatusCode
                resp.Body.Close()
            }
        }()
    }
    
    wg.Wait()
    close(results)
    
    successCount := 0
    for statusCode := range results {
        if statusCode == http.StatusOK {
            successCount++
        }
    }
    
    assert.Equal(t, numRequests, successCount)
}
```

### Docker Support

```dockerfile
# docker/Dockerfile
FROM golang:1.21-alpine AS builder

WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -o metrics-api cmd/server/main.go

FROM alpine:latest
RUN apk --no-cache add ca-certificates
WORKDIR /root/

COPY --from=builder /app/metrics-api .

EXPOSE 8080
CMD ["./metrics-api"]
```

### Deployment

```bash
# Build and run
docker build -f docker/Dockerfile -t metrics-api .
docker run -p 8080:8080 \
  -e PROMETHEUS_URL=http://host.docker.internal:9090 \
  metrics-api
```

### Usage Examples

```bash
# Get system metrics
curl http://localhost:8080/api/v1/metrics/system | jq .

# Get portrait application metrics  
curl http://localhost:8080/api/v1/metrics/portrait | jq .

# Health check
curl http://localhost:8080/health
```

This design provides a robust, testable Go microservice that aggregates Prometheus metrics and exposes them via a clean REST API with proper error handling, logging, and observability.