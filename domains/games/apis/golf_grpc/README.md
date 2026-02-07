# Golf gRPC Service - C++ Implementation

This directory contains the gRPC service implementation for the golf application. It provides the remote procedure call interface for clients to interact with the golf service.

## Related Projects

This gRPC service is part of a larger golf application ecosystem. Related components can be found in:
- [Core Golf Service](../golf_service) - Backend implementation
- [Original Golf UI](../../web/golf_ui) - Original web interface
- [Updated Golf UI](../../web/golf_ui_2) - Modernized web interface

## Features

- gRPC service definition and implementation
- Protocol buffer message handling
- Client-server communication
- Service discovery and registration

## Building

This project uses Bazel for building:

```bash
bazel build //domains/games/apis/golf_grpc:...
```

## Testing

```bash
bazel test //domains/games/apis/golf_grpc:...
```

## Protocol Buffers

This service uses protocol buffers for data serialization and gRPC communication. The proto definitions can be found in the [protos](../../protos) directory.

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
