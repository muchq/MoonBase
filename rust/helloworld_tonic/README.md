# Hello World gRPC - Rust Tonic Implementation

This directory contains a Rust implementation of a simple gRPC service using the Tonic framework. It serves as an example and reference for implementing gRPC services in Rust.

## Features

- gRPC service implementation using Tonic
- Protocol buffer integration
- Client-server communication
- Error handling

## Build
```
bazel build //rust/helloworld_tonic:...
```

## Test

```bash
bazel test //rust/helloworld_tonic:...
```

## Running the Service

```bash
# Start the server
bazel run //rust/helloworld_tonic:server

# In another terminal, run the client
bazel run //rust/helloworld_tonic:client
```

## Protos
See [proto defs here](../../protos/example_service/helloworld.proto)
