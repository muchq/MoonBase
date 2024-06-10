#include <gtest/gtest.h>

#include "cpp/example_service/example_service.h"
#include "protos/example_cc_grpc/example_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>


#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/message_allocator.h>

using example_service::Greeter;
using example_service::HelloRequest;
using example_service::HelloReply;
using grpc::ServerBuilder;
using grpc::Server;
using grpc::Status;
using grpc::ClientContext;

TEST(SERVICE_TEST, BasicAssertions) {
  GreeterServiceImpl service;

  ServerBuilder builder;
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  auto stub = Greeter::NewStub(server->InProcessChannel({}));

  ClientContext context;
  HelloRequest req;
  HelloReply res;
  req.set_name("Test Name");

  Status status = stub->SayHello(&context, req, &res);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(res.message(), "Hello Test Name");

  server->Shutdown();
}
