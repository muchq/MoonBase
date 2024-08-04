# Escapist

This seemed easier than figuring out how to build the mongocxx driver with Bazel.

### No Reflection
```shell
grpcurl -proto protos/escapist/escapist.proto \
    -rpc-header "db-name: demo"\
    -d '{"id": "foo123", "collection": "golf"}'\
    -plaintext localhost:50051 \
    escapist.Escapist/FindDocById
```