#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdlib>

#include "cpp/cards/golf/doc_db_game_store.h"
#include "cpp/doc_db_client/doc_db_client.h"
#include "cpp/golf_grpc/server/golf_grpc_service.h"

using grpc::Server;
using grpc::ServerBuilder;

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

  auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  auto stub = std::make_shared<doc_db::DocDb::Stub>(doc_db::DocDb::Stub(channel));
  auto client = std::make_shared<doc_db::DocDbClient>(doc_db::DocDbClient{stub, "golf"});
  auto game_store = std::make_shared<golf::DocDbGameStore>(golf::DocDbGameStore{client});
  golf::GameManager game_manager{game_store};
  GolfServiceImpl service{game_manager};

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());  // no auth
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

uint16_t ReadPort(uint16_t default_port) {
  if (const char* env_p = std::getenv("PORT")) {
    return static_cast<uint16_t>(std::atoi(env_p));
  }
  return default_port;
}

int main() {
  RunServer(ReadPort(8080));
  return 0;
}
