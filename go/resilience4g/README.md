# Resilience4g - Go Implementation

This directory contains a Go implementation of resilience patterns inspired by Resilience4j. It provides a set of utilities for building resilient microservices in Go.

## Features

- Circuit breaker pattern
- Rate limiting
- Retry mechanisms
- Bulkhead pattern
- Timeout handling
- Fallback strategies

## Building

This project uses both Go modules and Bazel for building:

```bash
# Using Go
go build ./...

# Using Bazel
bazel build //go/resilience4g:...
```

## Testing

```bash
# Using Go
go test ./...

# Using Bazel
bazel test //go/resilience4g:...
```

## Example Usage

```go
// Example of using the circuit breaker
breaker := resilience4g.NewCircuitBreaker(
    "my-service",
    resilience4g.WithFailureThreshold(5),
    resilience4g.WithResetTimeout(30 * time.Second),
)

result, err := breaker.Execute(func() (interface{}, error) {
    return someService.Call()
})
```
