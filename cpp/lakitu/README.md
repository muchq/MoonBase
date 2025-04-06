# Lakitu Service - C++ Implementation

This directory contains a C++ implementation of the Lakitu service. It provides cloud infrastructure management and monitoring capabilities.

## Features

- Cloud resource management
- Infrastructure monitoring
- Resource provisioning
- Health checking
- Metrics collection

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/lakitu:...
```

## Testing

```bash
bazel test //cpp/lakitu:...
```

## Running the Service

```bash
bazel run //cpp/lakitu:server
```

## Protocol Buffers

This service uses protocol buffers for data serialization. The proto definitions can be found in the [protos](../../protos) directory.

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
