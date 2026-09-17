#include "domains/ai/libs/deja_cpp/client.h"

#include <utility>

namespace deja {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "MoonBase games_hub/1.0";
  config.request_timeout_ms = 2'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::deja::DejaClient::Create(std::move(config));
  if (!client.ok()) {
    return std::move(client).error();
  }
  return Client(std::move(*client));
}

opal::Outcome<moonbase::deja::FetchRecentOutput> Client::Recent(std::int64_t after) const {
  return client_.FetchRecent(moonbase::deja::FetchRecentInput{.after = after});
}

}  // namespace deja
