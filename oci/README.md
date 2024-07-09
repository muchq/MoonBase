# OCI

```shell
sudo apt install podman
bazel run //oci:example_cc_grpc_image_tarball
podman run -p 8080:8089 example_cc_grpc
grpcurl -d '{"name": "Friend"}' -plaintext localhost:8080 example_service.Greeter/SayHello
```
