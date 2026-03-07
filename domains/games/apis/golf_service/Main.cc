#include <grpcpp/create_channel.h>

#include <iostream>

#include "absl/log/initialize.h"
#include "domains/games/libs/cards/golf/doc_db_game_store.h"
#include "domains/platform/libs/doc_db_client/doc_db_client.h"
#include "domains/games/apis/golf_service/router.h"
#include "mongoose.h"

namespace {
struct RouterHolder {
  std::optional<golf_service::Router> router_;
};

RouterHolder rh;

void do_route(struct ::mg_connection* c, int ev, void* ev_data) {
  rh.router_.value().route(c, ev, ev_data);
}
}  // namespace

int main() {
  struct mg_mgr mgr{};
  mg_mgr_init(&mgr);

  // init stuff here
  absl::InitializeLog();

  auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  auto stub = std::make_shared<doc_db::DocDb::Stub>(doc_db::DocDb::Stub(channel));
  auto client = std::make_shared<doc_db::DocDbClient>(doc_db::DocDbClient{stub, "golf"});
  auto game_store = std::make_shared<golf::DocDbGameStore>(golf::DocDbGameStore{client});
  auto game_manager = std::make_shared<golf::GameManager>(game_store);
  auto handler = std::make_shared<golf_service::Handler>(golf_service::Handler{game_manager});
  rh.router_ = golf_service::Router{handler};

  auto socket = mg_http_listen(&mgr, "http://0.0.0.0:8000", do_route, nullptr);
  if (socket == nullptr || !socket->is_listening) {
    std::cout << "failed to bind port to 8000\n";
    return 1;
  }
  std::cout << "listening on port 8000\n";
  for (;;) {
    mg_mgr_poll(&mgr, 500);
  }
  mg_mgr_free(&mgr);
  return 0;
}
