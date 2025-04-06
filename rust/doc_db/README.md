# Document Database - Rust Implementation

This directory contains a Rust implementation of a document database system backed by MongoDB.

## Related Projects

This implementation is part of a multi-language document database system. Related implementations can be found in:
- [Go Implementation](../../go/doc_db)
- [C++ Client Implementation](../../cpp/doc_db_client)

## Features
- Document storage and retrieval
- Indexing WIP
- Concurrent access support

### No Reflection at the Moment
```shell
grpcurl -proto protos/doc_db/doc_db.proto \
    -rpc-header "db_namespace: demo"\
    -d '{"id": "foo123", "collection": "golf"}'\
    -plaintext localhost:50051 \
    doc_db.DocDb/FindDocById
```
