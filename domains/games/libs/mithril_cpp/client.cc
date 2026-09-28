#include "domains/games/libs/mithril_cpp/client.h"

#include <utility>

namespace mithril {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "games_hub/1.0";
  config.request_timeout_ms = 2'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::mithril::MithrilClient::Create(std::move(config));
  if (!client.ok()) {
    return std::move(client).error();
  }
  return Client(std::move(*client));
}

opal::Outcome<std::optional<std::vector<std::string>>> Client::Wordchain(std::string start,
                                                                         std::string end) const {
  moonbase::mithril::WordchainInput input;
  input.start = std::move(start);
  input.end = std::move(end);
  auto output = client_.Wordchain(input);
  if (!output.ok()) {
    return std::move(output).error();
  }
  return std::move(output->path);
}

}  // namespace mithril
