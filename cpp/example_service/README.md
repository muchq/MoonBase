# Example Service - C++ Implementation

This directory contains a C++ implementation of an example service. It serves as a reference implementation for building C++ services in this repository.

## Features

- Service architecture
- Protocol buffer integration
- Error handling
- Logging
- Configuration management

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/example_service:...
```

## Testing

```bash
bazel test //cpp/example_service:...
```

## Running the Service

```bash
bazel run //cpp/example_service:server
```

## Protocol Buffers

This service uses protocol buffers for data serialization. The proto definitions can be found in the [protos](../../protos) directory.

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
