# Build Pal - Rust Implementation

A unified command-line interface for triggering build and test actions across different project types (Bazel, Maven, Gradle).

## Overview

Build Pal provides a consistent interface for building projects regardless of the underlying build tool. This Rust workspace contains the core implementation including the CLI client, server, and core libraries.

**Looking for usage documentation?** See [BUILD_PAL.md](../../BUILD_PAL.md) in the repository root for quick start guides and user documentation.

## Architecture

```
rust/build_pal/
├── cli/          # Command-line interface (build_pal_cli binary)
├── core/         # Shared types, config parsing, and business logic
├── server/       # Background server for async build execution
├── scripts/      # Build and deployment scripts
└── tests/        # Integration tests
```

### Components

- **core** - Foundation library containing:
  - Configuration types and parsing
  - Build tool abstractions
  - Error handling with structured logging
  - Shared domain models

- **cli** - Command-line client that:
  - Parses user commands
  - Loads and validates `.build_pal` configuration
  - Communicates with the server for async builds
  - Streams output for sync builds

- **server** - Background service that:
  - Manages build queues and execution
  - Provides REST API for build requests
  - Handles WebSocket connections for log streaming
  - Manages retention policies and cleanup

## Development Setup

### Prerequisites

- **Rust 1.88+** - Required by Bazel rules_rust
- **Bazel** - [Install from bazel.build](https://bazel.build/)
- Your target build tools (Bazel, Maven, or Gradle) for testing

### Building

```bash
# Build all components
bazel build //rust/build_pal/cli:build_pal_cli
bazel build //rust/build_pal/server:build_pal_server

# Build everything in the workspace
bazel build //rust/build_pal:all

# Build specific targets
bazel build //rust/build_pal/core:build_pal_core
```

Binaries are located at:
- `bazel-bin/rust/build_pal/cli/build_pal_cli`
- `bazel-bin/rust/build_pal/server/build_pal_server`

### Testing

```bash
# Run all tests
bazel test //rust/build_pal:all

# Run all tests in the entire workspace
bazel test //...

# Run specific test suites
bazel test //rust/build_pal:end_to_end_integration_test
bazel test //rust/build_pal:error_handling_test
bazel test //rust/build_pal:build_pipeline_integration_test
bazel test //rust/build_pal:deployment_test

# Run tests for specific components
bazel test //rust/build_pal/cli:build_pal_cli_test
bazel test //rust/build_pal/core:build_pal_core_test
bazel test //rust/build_pal/server:build_pal_server_test
bazel test //rust/build_pal/server:integration_tests
```

### Querying the Build Graph

```bash
# See all targets in build_pal
bazel query //rust/build_pal/...

# See all test targets
bazel query 'kind(.*_test, //rust/build_pal/...)'

# See dependencies of a target
bazel query 'deps(//rust/build_pal/cli:build_pal_cli)'

# See reverse dependencies
bazel query 'rdeps(//rust/build_pal/..., //rust/build_pal/core:build_pal_core)'
```

## Project Structure

### Core Types

The `core` crate defines the primary domain models:

```rust
// Build tools
pub enum BuildTool {
    Bazel,
    Maven,
    Gradle,
}

// Execution modes
pub enum ExecutionMode {
    Sync,   // Stream output to CLI
    Async,  // Background execution
}

// Configuration
pub struct CLIConfig {
    pub tool: BuildTool,
    pub name: String,
    pub mode: ExecutionMode,
    pub retention: RetentionPolicy,
    pub environment: Environment,
    // ...
}
```

### Error Handling

Build Pal uses structured error handling with the `BuildPalError` type:

```rust
use build_pal_core::{BuildPalError, log_build_pal_error};

fn example() -> Result<(), BuildPalError> {
    let result = risky_operation()
        .map_err(|e| BuildPalError::validation(format!("Invalid input: {}", e)))?;

    // Errors are automatically logged with context
    log_build_pal_error(&error, Some("operation_name"));

    Ok(())
}
```

### Adding a New Build Tool

To add support for a new build tool:

1. Add variant to `BuildTool` enum in `core/src/types.rs`
2. Implement command translation in the appropriate module
3. Add integration tests in `tests/`
4. Update configuration validation
5. Update BUILD.bazel files if adding new dependencies
6. Add examples to documentation

## Testing Strategy

### Unit Tests
- Located alongside source code in each crate
- Focus on individual functions and modules
- Automatically discovered by Bazel rules_rust

### Integration Tests
- Located in `tests/` directory and `server/tests/`
- Test end-to-end workflows
- Include:
  - `build_pipeline_integration_test` - Full build pipeline
  - `end_to_end_integration_test` - CLI to server flow
  - `error_handling_test` - Error scenarios
  - `deployment_test` - Packaging and installation

### Running Tests with Options

```bash
# Run tests with verbose output
bazel test //rust/build_pal:all --test_output=all

# Run tests with specific filters
bazel test //rust/build_pal:all --test_filter=test_name

# Run tests and show timing
bazel test //rust/build_pal:all --test_summary=detailed
```

## Deployment

### Building Distribution

```bash
# Build and package for distribution
./scripts/build.sh

# This creates dist/ directory with:
# - build_pal (CLI binary)
# - build_pal_server (server binary)
# - install.sh (installation script)
# - VERSION file
```

### Testing Deployment

```bash
# Run deployment tests
bazel test //rust/build_pal:deployment_test

# Manually test installation
./scripts/test-deployment.sh
cd dist
./install.sh --install-dir /tmp/build_pal_test
```

## Configuration

Build Pal reads configuration from `.build_pal` JSON files. See the [main documentation](../../BUILD_PAL.md#configuration-options) for examples.

The configuration parser (`ConfigParser` in `cli/src/config.rs`) supports:
- Auto-discovery (searches up directory tree)
- Explicit path via `--config` flag
- Validation of all fields
- Sensible defaults

## Server API

The server exposes a REST API on port 8080:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/builds` | POST | Submit build request |
| `/api/builds/{id}` | GET | Get build status |
| `/api/builds/{id}` | DELETE | Cancel build |
| `/api/builds/{id}/logs` | GET | Stream logs (WebSocket) |

## Logging

Build Pal uses structured logging via `tracing`:

```rust
use build_pal_core::{LoggingConfig, LogLevel};

// Initialize logging
let config = LoggingConfig::for_component("my_component")
    .with_level(LogLevel::Debug);
config.init()?;

// Logs include structured context
tracing::info!(build_id = "123", "Build started");
```

## Development Workflow

### Making Changes

1. Edit source files in `cli/`, `core/`, or `server/`
2. Build to check for compilation errors:
   ```bash
   bazel build //rust/build_pal:all
   ```
3. Run tests to verify functionality:
   ```bash
   bazel test //rust/build_pal:all
   ```
4. Run the full test suite:
   ```bash
   bazel test //...
   ```

### Debugging Builds

```bash
# See detailed build information
bazel build //rust/build_pal/cli:build_pal_cli --subcommands

# Run with sandbox debugging
bazel build //rust/build_pal/cli:build_pal_cli --sandbox_debug

# Clean and rebuild
bazel clean
bazel build //rust/build_pal:all
```

### Updating Dependencies

Dependencies are managed in `Cargo.toml` files and resolved by Bazel's rules_rust. After modifying dependencies:

```bash
# Rebuild to fetch new dependencies
bazel build //rust/build_pal:all

# Check dependency tree
bazel query 'deps(//rust/build_pal/cli:build_pal_cli)' --output graph
```

## Common Tasks

### Running the CLI Locally

```bash
# Build first
bazel build //rust/build_pal/cli:build_pal_cli

# Run the CLI
bazel-bin/rust/build_pal/cli/build_pal_cli --help

# Or use bazel run
bazel run //rust/build_pal/cli:build_pal_cli -- --help
```

### Running the Server Locally

```bash
# Build first
bazel build //rust/build_pal/server:build_pal_server

# Run the server
bazel-bin/rust/build_pal/server/build_pal_server

# Or use bazel run
bazel run //rust/build_pal/server:build_pal_server
```

### Testing Changes End-to-End

```bash
# Terminal 1: Start the server
bazel run //rust/build_pal/server:build_pal_server

# Terminal 2: Run the CLI
bazel run //rust/build_pal/cli:build_pal_cli -- build //...
```

## Troubleshooting

### Build Issues

```bash
# Clean everything and rebuild
bazel clean --expunge
bazel build //rust/build_pal:all

# Check for build errors
bazel build //rust/build_pal:all --verbose_failures
```

### Test Failures

```bash
# Run tests with full output
bazel test //rust/build_pal:all --test_output=all

# Run specific test with debugging
bazel test //rust/build_pal:end_to_end_integration_test --test_output=streamed
```

### Bazel Cache Issues

```bash
# Clear Bazel cache
bazel clean

# Clear all Bazel state
bazel clean --expunge
```

## Resources

- **User Guide**: [BUILD_PAL.md](../../BUILD_PAL.md)
- **Specifications**: `.kiro/specs/build-pal/`
- **Examples**: See `tests/` for comprehensive examples
- **Build Files**: Check `BUILD.bazel` files for target definitions

## Contributing

### Pull Request Process

1. Create a feature branch
2. Make your changes
3. Update BUILD.bazel files if adding new dependencies
4. Add tests for new functionality
5. Run full test suite: `bazel test //...`
6. Submit PR with clear description

### Build File Guidelines

- Keep BUILD.bazel files clean and well-organized
- Add comments for non-obvious dependencies
- Use consistent naming conventions for targets
- Ensure all new code has corresponding test targets

## License

See repository root for license information.
