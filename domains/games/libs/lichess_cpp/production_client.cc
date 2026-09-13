#include "domains/games/libs/lichess_cpp/production_client.h"

#include <utility>

#include "opal/http/beast_transport.h"

namespace lichess {

opal::Outcome<Client> CreateProductionClient(std::string_view token) {
  opal::ClientConfig config = WithBearerToken(DefaultClientConfig(), token);
  auto transport = opal::http::BeastHttpClient::FromConfig(config);
  if (!transport.ok()) {
    return std::move(transport).error();
  }
  config.http_client = *std::move(transport);
  return Client::Create(std::move(config));
}

}  // namespace lichess
