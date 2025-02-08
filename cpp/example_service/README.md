# Example GRPC Service

### Build
```
bazel build //cpp/example_service
```

### Run
```
bazel-bin/cpp/example_service/example_service
```

### Call
```
➜  ~ grpcurl -d '{"name": "Friend"}' -plaintext localhost:8080 example_service.Greeter/SayHello
{
  "message": "Hello Friend"
}
```

### OCI
# OCI
```shell
sudo apt install podman
bazel run //cpp/example_service:oci_tarball
podman run -p 8080:8089 example_cc_grpc
grpcurl -d '{"name": "Friend"}' -plaintext localhost:8080 example_service.Greeter/SayHello
```
