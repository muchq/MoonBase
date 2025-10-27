# Build Pal - Quick Start Guide

Build Pal is a unified command-line interface for triggering build and test actions across different project types (Bazel, Maven, Gradle).

## Architecture Overview

Build Pal consists of three main components:

1. **CLI Client** (`build_pal`) - Command-line interface for submitting build requests
2. **Server** (`build_pal_server`) - Background service that executes builds and manages state
3. **Web UI** - Browser-based interface for viewing build logs and status (React/Vite)

## Prerequisites

- Rust 1.88+ (for building from source)
- Node.js 18+ (for web UI)
- Bazel (for building with Bazel)
- Your target build tool (Bazel, Maven, or Gradle)

## Building from Source

### Option 1: Using Bazel (Recommended)

```bash
# Build all components
bazel build //rust/build_pal/cli:build_pal_cli
bazel build //rust/build_pal/server:build_pal_server

# Build web UI
cd web/build_pal
npm install
npm run build
```

### Option 2: Using Cargo

```bash
# Build CLI and server
cd rust/build_pal
cargo build --release --bin build_pal_cli
cargo build --release --bin build_pal_server

# Binaries will be in target/release/
```

### Option 3: Using Build Script

```bash
# Automated build with packaging
cd rust/build_pal
./scripts/build.sh

# This creates a dist/ directory with all binaries and installation script
```

## Running Build Pal

### 1. Start the Server

The server must be running before using the CLI or web UI.

```bash
# Using Bazel-built binary
bazel-bin/rust/build_pal/server/build_pal_server

# Using Cargo-built binary
./target/release/build_pal_server

# Using packaged binary
./dist/build_pal_server
```

The server will start on `http://localhost:8080` by default.

### 2. Start the Web UI (Development)

```bash
cd web/build_pal
npm install
npm run dev
```

The web UI will be available at `http://localhost:5173` (or the port shown in terminal).

### 3. Configure Your Project

Create a `.build_pal` configuration file in your project root:

#### Bazel Project Example
```json
{
  "tool": "bazel",
  "name": "my-bazel-project",
  "description": "My awesome Bazel project",
  "mode": "async",
  "retention": "all",
  "retention_duration_days": 14,
  "environment": "native"
}
```

#### Maven Project Example
```json
{
  "tool": "maven",
  "name": "my-maven-project",
  "mode": "sync",
  "retention": "error",
  "retention_duration_days": 30,
  "environment": "native"
}
```

#### Gradle Project with Docker Example
```json
{
  "tool": "gradle",
  "name": "my-gradle-project",
  "mode": "async",
  "retention": "all",
  "environment": "docker",
  "docker": {
    "image": "openjdk:11",
    "workdir": "/workspace",
    "volumes": ["./:/workspace"],
    "environment": {
      "GRADLE_OPTS": "-Xmx2g"
    },
    "rsync_options": ["--exclude=build/", "--exclude=.gradle/"],
    "sync_strategy": "pre-build"
  }
}
```

### 4. Use the CLI

```bash
# Using Bazel-built binary
bazel-bin/rust/build_pal/cli/build_pal_cli [command]

# Using Cargo-built binary
./target/release/build_pal_cli [command]

# Using packaged binary
./dist/build_pal [command]
```

#### CLI Examples

```bash
# Build your project
build_pal build //...

# Run tests
build_pal test

# Maven-specific commands
build_pal clean compile
build_pal package

# Gradle-specific commands
build_pal clean build
build_pal assemble

# Override execution mode
build_pal build //... --sync    # Force synchronous mode
build_pal build //... --async   # Force asynchronous mode

# Cancel a running build
build_pal --cancel <build-id>

# Use custom config file
build_pal build //... --config /path/to/.build_pal
```

## Configuration Options

### Execution Modes
- **async**: Build runs in background, returns immediately with build ID and web URL
- **sync**: Build runs in foreground, streams output to terminal

### Retention Policies
- **all**: Keep all build logs and artifacts
- **error**: Keep only failed builds

### Environment Types
- **native**: Run builds directly on host system
- **docker**: Run builds in Docker containers (requires Docker config)

## Troubleshooting

### Server Not Starting
```bash
# Check if port 8080 is already in use
lsof -i :8080

# Check server logs
build_pal_server 2>&1 | tee server.log
```

### CLI Can't Connect to Server
```bash
# Verify server is running
curl http://localhost:8080/health

# Check if server is on different port
build_pal --help  # Shows current server configuration
```

### Build Failures
```bash
# Check build logs in web UI at http://localhost:5173
# Or check server logs for detailed error information

# Verify your build tool is installed and working
bazel version    # For Bazel projects
mvn --version    # For Maven projects
gradle --version # For Gradle projects
```

### Configuration Issues
```bash
# Validate your .build_pal file
cat .build_pal | jq .  # Should parse as valid JSON

# Check available commands for your build tool
build_pal --help
```

## Development

### Running Tests

```bash
# Run all tests
bazel test //rust/build_pal:all

# Run specific test suites
bazel test //rust/build_pal:end_to_end_integration_test
bazel test //rust/build_pal:error_handling_test
bazel test //rust/build_pal:deployment_test
```

### Testing Deployment

```bash
# Test the build and packaging process
cd rust/build_pal
./scripts/build.sh
./scripts/test-deployment.sh
```

## Production Deployment

### Using Installation Script

```bash
# Build distribution
cd rust/build_pal
./scripts/build.sh

# Install to system
cd dist
./install.sh

# Or install to custom directory
./install.sh --install-dir /usr/local/bin
```

### Manual Installation

```bash
# Copy binaries to PATH
sudo cp dist/build_pal /usr/local/bin/
sudo cp dist/build_pal_server /usr/local/bin/
sudo chmod +x /usr/local/bin/build_pal*
```

### Running as Service (systemd)

Create `/etc/systemd/system/build-pal-server.service`:

```ini
[Unit]
Description=Build Pal Server
After=network.target

[Service]
Type=simple
User=buildpal
ExecStart=/usr/local/bin/build_pal_server
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable build-pal-server
sudo systemctl start build-pal-server
sudo systemctl status build-pal-server
```

## API Reference

The server exposes a REST API on port 8080:

- `GET /health` - Health check
- `POST /api/builds` - Submit build request
- `GET /api/builds/{id}` - Get build status
- `DELETE /api/builds/{id}` - Cancel build
- `GET /api/builds/{id}/logs` - Get build logs (WebSocket)

## Support

- **Documentation**: See `.kiro/specs/build-pal/` for detailed requirements and design
- **Issues**: Check server logs and web UI for error details
- **Configuration**: Refer to example `.build_pal` files above

## Version Information

Check version and build information:

```bash
# CLI version
build_pal --version

# Server version (check logs on startup)
build_pal_server

# Distribution version
cat dist/VERSION
```