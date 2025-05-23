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

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/golf_service:...
```

## Testing

```bash
bazel test //cpp/golf_service:...
```

## Protocol Buffers

This service uses protocol buffers for data serialization and gRPC communication. The proto definitions can be found in the [protos](../../protos) directory.

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
