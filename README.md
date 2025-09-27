# MoonBase

![Test + Publish](https://github.com/muchq/MoonBase/actions/workflows/publish.yml/badge.svg)

![MoonBase](static_content/moon.gif)

## Contents
### Web Stuff
- **Go**:
  - [`mucks`](go/mucks) - Router wrapper middleware
  - [`resilience4g`](go/resilience4g) - Resilience4J for Go
  - [`r3dr`](go/r3dr) - a url shortening service
  - [`games_ws_backend`](go/games_ws_backend) - websocket backend for [some games on muchq.com](https://muchq.com)
  - [`thoughts`](go/thoughts) - websocket backend for this [thing that's sort of a game?](https://muchq.com/thoughts)
  - [`doc_db`](go/doc_db) - DocDB WIP
  - [`example_grpc`](go/example_grpc) - gRPC example
- **C++**:
  - [`meerkat`](cpp/meerkat/) - Mongoose wrapper
  - [`lakitu`](cpp/lakitu) - gRPC wrapper
  - [`example_service`](cpp/example_service) - Example gRPC service
  - [`doc_db_client`](cpp/doc_db_client) - Document database client
- **Java**:
  - [`lunarcat`](jvm/src/main/java/com/muchq/lunarcat) - Rest-style HTTP server
  - [`yochat`](jvm/src/main/java/com/muchq/yochat) - Chat server
- **Rust**:
  - [`doc_db`](rust/doc_db) - DocDB with Mongo backend
  - [`helloworld_tonic`](rust/helloworld_tonic) - gRPC example using Tonic

### Graphics Stuff
- **C++**:
    - [`tracy`](cpp/tracy) - ray tracer with some neat knobs
    - [`trill`](cpp/trill) - SDL3 helpers
    - [`mandelbrot`](cpp/mandelbrot) - a basic cpu mandelbrot renderer
- **Java**:
    - [`imagine`](jvm/src/main/java/com/muchq/imagine) - a couple of blur filters and some edge detection
- **Rust**:
    - [`imagine`](rust/imagine) - port of java imagine lib
- **Go**:
    - [`tracy`](go/tracy) - ray tracer
    - [`blurring`](go/images) - some image processing tools
- **Scala**:
  - [`scraphics`](jvm/src/main/scala/com/muchq/scraphics) - a basic ray tracer

### AI Stuff
- **Go**:
  - [`neuro`](go/neuro) - using LLMs to learn about Neural Networks
- **Python**:
  - [`pytorch_hello`](python/pytorch_hello) - just starting learn about pytorch

### Card Games
- **Rust**:
    - [`/rust/cards`](rust/cards) - Experimenting with moving card game engines to Rust
- **C++**:
    - [`/cpp/cards`](cpp/cards) - Golf implementation and some helpers
    - [`golf_service`](cpp/golf_service) - Golf websocket server
    - [`golf_grpc`](cpp/golf_grpc) - Golf gRPC service
- **Web**: (UIs mostly live [here](https://github.com/muchq/muchq.github.io) now)
    - [`golf_ui`](web/golf_ui) - Original golf UI
    - [`golf_ui_2`](web/golf_ui_2) - Updated golf UI
- **Go**:
    - [`/go/cards`](go/cards) - Experimenting with moving card game engines to Go

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
- **JVM**:
  - [`misc stuff`](jvm)

## IDE Support
### IntelliJ
Tested with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)

Java, Go, and Python targets Just Work™.

Add new targets to [project view](/.ijwb/.bazelproject) if they aren't detected automatically.

### Clion
C++ and Rust projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij), but autocomplete/intellisense doesn't feel very snappy.

### VSCode
For C++, [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor) works pretty well if you like vscode.

Follow instructions [here](https://github.com/hedronvision/bazel-compile-commands-extractor#vscode)

and then do
```
bazel run @hedron_compile_commands//:refresh_all
code .
```

## Importing a project?
See [IMPORTING](./IMPORTING.md)
