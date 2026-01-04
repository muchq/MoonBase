# Hello World Java HTTP Server

A simple HTTP server built with Micronaut using JAX-RS style annotations and Netty.

### Build the Java binary
```bash
bazel build //jvm/src/main/java/com/muchq/helloworld:helloworld
```

### Build the Docker image

```bash
bazel build //jvm/src/main/java/com/muchq/helloworld:oci_tarball
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
