#include "domains/games/libs/lichess_cpp/client.h"

#include <chrono>
#include <string>
#include <utility>

namespace lichess {

opal::ClientConfig DefaultClientConfig() {
  opal::ClientConfig config;
  config.endpoint = "https://lichess.org";
  config.user_agent = "MoonBase indexer/1.0";
  // Per attempt, as opal applies it — and sized from Lichess's throttle
  // rather than from habit. The export streams at about 20 games/second
  // anonymously and 30 authenticated, so a minute only covers a month of
  // roughly 1,200 games: an active bullet player exceeds that, and the
  // retry would restart the same response rather than resume it, so the
  // month could never finish. Ten minutes covers about 12,000 games
  // anonymous, 18,000 with a token.
  //
  // It also sets what shutdown has to drain: a run cannot interrupt an
  // export it is already inside.
  config.request_timeout_ms = 600'000;
  config.retry.max_attempts = 3;
  config.retry.initial_backoff = std::chrono::seconds(1);
  // Lichess asks for one request at a time and its 429 cooldown has been
  // observed to outlast 75 seconds, so the ceiling is higher than
  // chess.com's. Retry-After, when sent, raises the floor above this.
  config.retry.max_backoff = std::chrono::seconds(90);
  return config;
}

opal::ClientConfig WithBearerToken(opal::ClientConfig config, std::string_view token) {
  if (token.empty()) {
    return config;
  }
  config.bearer_token = [owned = std::string(token)]() { return owned; };
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  // Read before the move, and from the config rather than from the token
  // that was passed in: WithBearerToken drops an empty one, so this is the
  // question of whether a header will actually be sent.
  const bool authenticated = config.bearer_token != nullptr;
  auto client = moonbase::lichess::LichessClient::Create(std::move(config));
  if (!client.ok()) {
    return std::move(client).error();
  }
  return Client(std::move(*client), authenticated);
}

opal::Outcome<moonbase::lichess::ExportGamesOutput> Client::ExportGames(
    std::string_view username, std::int64_t since_ms, std::int64_t until_ms) const {
  moonbase::lichess::ExportGamesInput input;
  input.username = std::string(username);
  input.since = since_ms;
  input.until = until_ms;
  // Asked for explicitly, because the default export sends no [ECO] and no
  // [Opening] — a real archive of seven games carries neither. Without this
  // every Lichess row stores a null opening, and opening.family answers
  // nothing for the platform.
  input.opening = true;
  input.accept = std::string(kPgnAccept);
  return client_.ExportGames(input);
}

}  // namespace lichess
