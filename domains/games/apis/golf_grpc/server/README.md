# Golf GRPC Service

### Build
```
bazel build //domains/games/apis/golf_grpc/server
```

### Run
```
bazel-bin/cpp/golf_grpc_service/golf_grpc/server
```

### Call
```
➜  ~ grpcurl -d '{"name": "Friend"}' -plaintext localhost:8088 example_service.Greeter/SayHello
{
  "message": "Hello Friend"
}
```

### OCI
```shell

sudo apt install podman
bazel run //domains/games/apis/golf_grpc/server:oci_tarball
podman run -p 8080:8089 golf_grpc_cc_grpc
grpcurl -d '{"name": "Bip"}' -plaintext localhost:8080 golf_grpc_service.Golf/RegisterUser
```
