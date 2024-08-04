#include "cpp/example_service/example_service.h"

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include "protos/example_service/helloworld.grpc.pb.h"

using example_service::Greeter;
using example_service::HelloReply;
using example_service::HelloRequest;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::Status;

TEST(SERVICE_TEST, BasicAssertions) {
  GreeterServiceImpl service;

  ServerBuilder builder;
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  auto channel = server->InProcessChannel({});
  auto stub = Greeter::NewStub(server->InProcessChannel({}));

  ClientContext context;
  context.AddMetadata("app-name", "test-app");

  HelloRequest req;
  HelloReply res;
  req.set_name("Test Name");

  Status status = stub->SayHello(&context, req, &res);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(res.message(), "Hello Test Name");

  server->Shutdown();
}
