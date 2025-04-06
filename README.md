# MoonBase

![Test + Publish](https://github.com/muchq/MoonBase/actions/workflows/publish.yml/badge.svg)

![MoonBase](static_content/moon.gif)

## IDE Support
### IntelliJ
Tested with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)

Java and Go targets Just Work™.

Add new targets to [project view](/.ijwb/.bazelproject) if they aren't detected automatically.

### Clion
C++ and Rust projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij), but autocomplete/intellisense doesn't feel very snappy.

### GoLand
Go projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij).
Alternatively, you can use IntelliJ for Go too.

### VSCode

For C++ use [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor)

Follow instructions [here](https://github.com/hedronvision/bazel-compile-commands-extractor#vscode)

and then do
```
bazel run @hedron_compile_commands//:refresh_all
code .
```

## Importing a project?
See [IMPORTING](./IMPORTING.md)

## Repository Contents

This repository contains multiple projects implemented across various programming languages. Here's a summary of the major components:

### Card Games
- **Rust**: [`/rust/cards`](rust/cards) - Experimenting with moving card game engines to Rust
- **C++**: [`/cpp/cards`](cpp/cards) - Golf rules implementation and some helpers
- **Go**: [`/go/cards`](go/cards) - Experimenting with moving card game engines to Go

### Document Database
- **Rust**: [`doc_db`](rust/doc_db) - DocDB with Mongo backend
- **Go**: [`doc_db`](go/doc_db) - DocDB WIP
- **C++**: [`doc_db_client`](cpp/doc_db_client) - Document database client

### Golf Services
- **C++**:
  - [`golf_service`](cpp/golf_service) - Golf websocket server
  - [`golf_grpc`](cpp/golf_grpc) - Golf gRPC service
- **Web**:
  - [`golf_ui`](web/golf_ui) - Original golf UI
  - [`golf_ui_2`](web/golf_ui_2) - Updated golf UI
 
### Web Related
- **C++**:
  - [`lakitu`](cpp/lakitu) - GRPC wrapper for C++
  - [`example_service`](cpp/example_service) - Example service
- **Java**:
  - [`lunarcat`](jvm/src/main/java/com/muchq/lunarcat) - Rest-style HTTP server
- **Rust**:
  - [`helloworld_tonic`](rust/helloworld_tonic) - gRPC example using Tonic
- **Go**:
  - [`resilience4g`](go/resilience4g) - Resilience4J for Go
  - [`r3dr`](go/r3dr) - a url shortening service
  - [`mucks`](go/mucks) - Router wrapper middleware
  - [`example_grpc`](go/example_grpc) - gRPC example

### Other Projects
- **Rust**:
  - [`wordchains`](rust/wordchains) - Word chain implementation
- **C++**:
  - [`so_leet`](cpp/so_leet) - Some leetcode
  - [`futility`](cpp/futility) - Utilities for C++
- **Go**:
  - [`images`](go/images) - Some image processing tools
  - [`clock`](go/clock) - a Clock
- **JS**:
  - [`flippymem`](web/flippymem) - Flippy memory game
- **JVM**: [`java and scala stuff`](jvm) - Java/Scala projects
