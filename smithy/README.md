# Smithy Server Generator

A code generator that creates server implementations from [AWS Smithy](https://smithy.io/) models for multiple languages.

## Supported Languages

- **Java** - Generates interfaces, handlers, routers, and data models
- **Go** - Generates interfaces, HTTP handlers, and types with JSON serialization
- **Rust** - Generates async traits, Axum handlers, and Serde-enabled types
- **C++** - Generates abstract classes, HTTP handlers with nlohmann/json

## Usage

### Bazel Build Rules

```starlark
load("//smithy/bazel:smithy.bzl", "smithy_java_server", "smithy_go_server", "smithy_rust_server", "smithy_cpp_server")

# Generate Java server
smithy_java_server(
    name = "myservice_java",
    model = ":myservice.json",
    package_name = "com.example.myservice",
)

# Generate Go server
smithy_go_server(
    name = "myservice_go",
    model = ":myservice.json",
    module_name = "myservice",
)

# Generate Rust server
smithy_rust_server(
    name = "myservice_rust",
    model = ":myservice.json",
    crate_name = "myservice",
)

# Generate C++ server
smithy_cpp_server(
    name = "myservice_cpp",
    model = ":myservice.json",
    module_name = "myservice",
)
```

### Command Line

```bash
# Java
bazel run //smithy/generators/java:generate -- model.json output/

# Go
bazel run //smithy/generators/go:generate -- model.json output/

# Rust
bazel run //smithy/generators/rust:generate -- model.json output/

# C++
bazel run //smithy/generators/cpp:generate -- model.json output/
```

## Project Structure

```
smithy/
├── core/                    # Core model parser and utilities
│   └── src/main/java/
│       └── com/moonbase/smithy/
│           ├── model/       # Smithy model representation
│           ├── parser/      # JSON AST parser
│           └── codegen/     # Code generation utilities
├── generators/
│   ├── java/               # Java server generator
│   ├── go/                 # Go server generator
│   ├── rust/               # Rust server generator
│   └── cpp/                # C++ server generator
├── runtime/
│   └── java/               # Java runtime library
├── examples/               # Example Smithy models
├── bazel/                  # Bazel build rules
└── README.md
```

## Example Model

See `examples/petstore.smithy` and `examples/petstore.json` for a complete example.

## Generated Code

### Java

- **Service Interface**: Async interface with CompletableFuture return types
- **Handler**: Abstract class with sync handler methods to implement
- **Router**: HTTP router that dispatches to the service
- **Models**: Immutable data classes with builders
- **Errors**: Exception classes with HTTP status codes

### Go

- **Service Interface**: Interface with context and error returns
- **Handler**: HTTP handler implementing http.Handler
- **Types**: Structs with JSON tags
- **Errors**: Error types implementing error interface

### Rust

- **Service Trait**: Async trait with Send + Sync bounds
- **Handler**: Axum router with state management
- **Types**: Structs with Serde derives and builders
- **Errors**: Error enum with thiserror integration

### C++

- **Service Class**: Pure virtual abstract class
- **Handler**: HTTP handler with nlohmann/json serialization
- **Types**: Structs with JSON conversion and builders
- **Errors**: Exception classes with HTTP status codes

## Running Tests

```bash
# Run all tests
bazel test //smithy/...

# Run specific test
bazel test //smithy/core:SmithyParserTest
bazel test //smithy/generators/java:JavaServerGeneratorTest
```

## Dependencies

The generators are written in Java and require:
- Gson for JSON parsing
- Guava for utilities

Generated code dependencies:

| Language | Dependencies |
|----------|-------------|
| Java | Runtime library (smithy-runtime) |
| Go | Standard library only |
| Rust | async-trait, axum, serde, tokio |
| C++ | nlohmann/json, C++23 |
