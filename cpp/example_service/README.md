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
➜  ~ grpcurl -d '{"name": "Friend"}' -plaintext localhost:8088 example_service.Greeter/SayHello
{
  "message": "Hello Friend"
}
```
