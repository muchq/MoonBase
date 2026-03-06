> [!WARNING]
> **Builds disabled.** This package is commented out until gRPC supports prebuilt protoc and Bazel 9.

# Golf gRPC Client - C++ Implementation

This directory contains a C++ client for the golf gRPC service. It provides a typed interface for calling the golf service over gRPC, with test coverage using gRPC mocks.

## Related Projects

- [`golf_grpc/server`](../server) — the server-side implementation
- [`games/protos/golf_grpc`](../../../protos/golf_grpc) — the proto definitions and generated CC/gRPC stubs
