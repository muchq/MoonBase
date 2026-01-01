# Hello World Java HTTP Server

A simple HTTP server built with Jetty that responds with JSON on all endpoints.

## Features

- Lightweight Jetty-based HTTP server
- JSON response format
- Configurable port via environment variable
- SLF4J logging with Logback
- Docker/OCI image support

## Building

### Build the Java binary

```bash
bazel build //jvm/src/main/java/com/muchq/helloworld:helloworld
```

### Build the Docker image

```bash
bazel build //jvm/src/main/java/com/muchq/helloworld:oci_tarball
```

## Running

### Run directly with Bazel

```bash
# Run on default port 8080
bazel run //jvm/src/main/java/com/muchq/helloworld:helloworld

# Run on custom port
PORT=9090 bazel run //jvm/src/main/java/com/muchq/helloworld:helloworld
```

### Run with Docker

```bash
# Load the image into Docker
bazel run //jvm/src/main/java/com/muchq/helloworld:oci_tarball

# Run the container
docker run -p 8080:8080 helloworld:latest

# Run on custom port
docker run -e PORT=9090 -p 9090:9090 helloworld:latest
```

## Testing

```bash
# Test the server
curl http://localhost:8080/hello

# Expected response:
{
  "message": "Hello, World!",
  "path": "/hello",
  "method": "GET"
}
```

## Configuration

- **PORT**: Server port (default: 8080)

## Architecture

- **Jetty 11.0.23**: HTTP server implementation
- **SLF4J + Logback**: Logging framework
- **Azul Zulu OpenJDK 25 JRE**: Base Docker image (multi-platform: linux/amd64, linux/arm64/v8)
- **Bazel**: Build system with OCI image support

## Deployment

The Docker image is production-ready and can be deployed to:
- Kubernetes (using the OCI image)
- Docker Swarm
- Cloud Run, ECS, or other container platforms
- Any OCI-compatible container runtime
