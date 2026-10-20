#include "domains/games/libs/go_engine_cpp/client.h"

#include <utility>

namespace go_engine {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "games_hub/1.0";
  // The service gives up at 10 s; 12 s leaves the connection room.
  config.request_timeout_ms = 12'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::goengine::GoEngineClient::Create(std::move(config));
  if (!client.ok()) return std::move(client).error();
  return Client(std::move(*client));
}

opal::Outcome<std::string> Client::Genmove(const Ask& ask) const {
  moonbase::goengine::GenmoveInput input;
  input.size = ask.size;
  input.komi = ask.komi;
  if (!ask.moves.empty()) input.moves = ask.moves;
  input.level = ask.level;
  if (ask.scoring) input.scoring = true;
  auto output = client_.Genmove(input);
  if (!output.ok()) return std::move(output).error();
  return std::move(output->answer);
}

}  // namespace go_engine
