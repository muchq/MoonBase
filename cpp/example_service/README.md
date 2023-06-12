# Example GRPC Service

### Build
```
bazel build //src/main/cpp/example_service
```

### Run
```
bazel-bin/src/main/cpp/example_service/example_service
```

### Call
```
➜  ~ grpcurl -d '{"name": "Friend"}' -plaintext localhost:8088 exampleservice.Greeter/SayHello
{
  "message": "Hello Friend"
}
```
