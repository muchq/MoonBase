# DocDb

This seemed easier than figuring out how to build the mongocxx driver with Bazel.

### No Reflection
```shell
grpcurl -proto protos/doc_db/doc_db.proto \
    -rpc-header "db-name: demo"\
    -d '{"id": "foo123", "collection": "golf"}'\
    -plaintext localhost:50051 \
    doc_db.DocDb/FindDocById
```
