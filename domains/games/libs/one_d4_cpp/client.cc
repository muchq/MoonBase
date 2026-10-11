#include "domains/games/libs/one_d4_cpp/client.h"

#include <utility>

namespace one_d4 {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "games_hub/1.0";
  config.request_timeout_ms = 5'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::oned4::OneD4Client::Create(std::move(config));
  if (!client.ok()) return std::move(client).error();
  return Client(std::move(*client));
}

opal::Outcome<std::string> Client::IndexMuchqMonth(const std::string& player_id,
                                                   const std::string& month) const {
  moonbase::oned4::CreateIndexInput input;
  input.player = player_id;
  input.platform = "MUCHQ_COM";
  input.startMonth = month;
  input.endMonth = month;
  auto output = client_.CreateIndex(input);
  if (!output.ok()) return std::move(output).error();
  return output->id.value_or("");
}

}  // namespace one_d4
