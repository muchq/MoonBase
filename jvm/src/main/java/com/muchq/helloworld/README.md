# Hello World Java HTTP Server

A simple HTTP server built with Micronaut using JAX-RS style annotations and Netty.

## Features

- Micronaut 4.10 with JAX-RS annotations
- Netty HTTP server (non-blocking)
- JSON responses
- Configurable port via environment variable
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
curl http://localhost:8080/

# Expected response:
{
  "message": "Hello, World!",
  "path": "/",
  "method": "GET"
}
```

## Configuration

- **PORT**: Server port (default: 8080)

## Architecture

- **Micronaut 4.10**: Compile-time dependency injection framework
- **Netty**: Non-blocking HTTP server
- **JAX-RS**: Jakarta RESTful Web Services annotations
- **Jackson**: JSON serialization

## Deployment

The OCI image can be deployed to Kubernetes, Cloud Run, ECS, or any container platform.
