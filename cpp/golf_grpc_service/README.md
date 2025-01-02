# Golf GRPC Service

### Build
```
bazel build //cpp/golf_grpc_service
```

### Run
```
bazel-bin/cpp/golf_grpc_service/golf_grpc_service
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
bazel run //cpp/golf_grpc_service:oci_tarball
podman run -p 8080:8089 golf_grpc_cc_grpc
grpcurl -d '{"name": "Bip"}' -plaintext localhost:8080 golf_grpc_service.Golf/RegisterUser
```
