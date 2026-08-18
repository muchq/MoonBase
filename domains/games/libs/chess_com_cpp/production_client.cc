#include "domains/games/libs/chess_com_cpp/production_client.h"

#include <utility>

#include "smithy/http/beast_transport.h"

namespace chess_com {

smithy::Outcome<Client> CreateProductionClient() {
  smithy::ClientConfig config = DefaultClientConfig();
  auto transport = smithy::http::BeastHttpClient::FromConfig(config);
  if (!transport.ok()) {
    return std::move(transport).error();
  }
  config.http_client = *std::move(transport);
  return Client::Create(std::move(config));
}

}  // namespace chess_com
