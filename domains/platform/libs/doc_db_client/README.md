# Document Database Client - C++ Implementation

This directory contains a C++ client implementation for interacting with the document database system. It provides a high-performance interface for C++ applications to access the document database.

## Related Projects

This client implementation is part of a multi-language document database system. Related implementations can be found in:
- [Rust Implementation](../../rust/doc_db)
- [Go Implementation](../../apis/doc_db_go)

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/doc_db_client:...
```

## Testing

```bash
bazel test //cpp/doc_db_client:...
```

## Protocol Buffers

This implementation uses protocol buffers for data serialization. The proto definitions can be found in the [protos](../../protos) directory.

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
