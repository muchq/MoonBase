#include "domains/games/libs/chess_engine_cpp/production_client.h"

#include <string>
#include <utility>

#include "opal/http/beast_transport.h"

namespace chess_engine {

opal::Outcome<Client> CreateProductionClient(std::string endpoint) {
  opal::ClientConfig config = DefaultClientConfig(std::move(endpoint));
  auto transport = opal::http::BeastHttpClient::FromConfig(config);
  if (!transport.ok()) return std::move(transport).error();
  config.http_client = *std::move(transport);
  return Client::Create(std::move(config));
}

}  // namespace chess_engine
