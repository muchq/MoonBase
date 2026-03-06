# Golf Service - C++ Implementation

This directory contains the core C++ implementation of the golf service. It provides the backend functionality for golf game management and scoring.

## Related Projects

This service is part of a larger golf application ecosystem. Related components can be found in:
- [Golf gRPC Service](../golf_grpc) - gRPC interface implementation
- [Original Golf UI](../../web/golf_ui) - Original web interface
- [Updated Golf UI](../../web/golf_ui_2) - Modernized web interface

## Features

- Golf game management
- Player scoring
- Course management
- Game state persistence

## Build
```bash
bazel build //domains/games/apis/golf_service:...
```

## Test

```bash
bazel test //domains/games/apis/golf_service:...
```
