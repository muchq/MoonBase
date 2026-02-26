#include <string>

#include "absl/strings/str_format.h"
#include "domains/games/apis/golf_grpc/server/golf_grpc_service.h"
#include "domains/games/libs/cards/golf/doc_db_game_store.h"
#include "domains/platform/libs/doc_db_client/doc_db_client.h"
#include "domains/platform/libs/lakitu/lakitu.h"

using lakitu::ReadPort;
using lakitu::Server;

void RunServer(uint16_t port) {
  const std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

  auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  auto stub = std::make_shared<doc_db::DocDb::Stub>(doc_db::DocDb::Stub(channel));
  auto client = std::make_shared<doc_db::DocDbClient>(doc_db::DocDbClient{stub, "golf"});
  auto game_store = std::make_shared<golf::DocDbGameStore>(golf::DocDbGameStore{client});
  auto game_manager = std::make_shared<golf::GameManager>(game_store);
  GolfServiceImpl service{game_manager};

  Server server;
  server.AddListeningPort(server_address, grpc::InsecureServerCredentials());  // no auth
  server.AddService(&service);
  server.StartAndWait();
}

int main() {
  RunServer(ReadPort(8080));
  return 0;
}
