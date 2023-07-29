#include <iostream>

#include "cpp/golf_service/router.h"
#include "mongoose.h"

int main() {
  struct mg_mgr mgr {};
  mg_mgr_init(&mgr);
  auto socket = mg_http_listen(&mgr, "http://0.0.0.0:8000", golf_service::router, nullptr);
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
