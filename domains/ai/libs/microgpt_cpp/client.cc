#include "domains/ai/libs/microgpt_cpp/client.h"

#include <utility>

namespace microgpt {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "MoonBase games_hub/1.0";
  config.request_timeout_ms = 5'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::microgpt::MicrogptClient::Create(std::move(config));
  if (!client.ok()) {
    return std::move(client).error();
  }
  return Client(std::move(*client));
}

opal::Outcome<moonbase::microgpt::ChatOutput> Client::Chat(
    std::vector<moonbase::microgpt::Message> messages, int max_tokens) const {
  moonbase::microgpt::ChatInput input;
  input.messages = std::move(messages);
  input.maxTokens = max_tokens;
  return client_.Chat(input);
}

}  // namespace microgpt
