#include "domains/games/libs/chess_engine_cpp/client.h"

#include <utility>

namespace chess_engine {

opal::ClientConfig DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "games_hub/1.0";
  // The service gives up at movetime plus 2 s; a bot thinks for well under
  // a second, so 5 s outlasts it with room for the connection.
  config.request_timeout_ms = 5'000;
  config.retry.max_attempts = 1;
  return config;
}

opal::Outcome<Client> Client::Create(opal::ClientConfig config) {
  auto client = moonbase::chessengine::ChessEngineClient::Create(std::move(config));
  if (!client.ok()) return std::move(client).error();
  return Client(std::move(*client));
}

opal::Outcome<std::string> Client::BestMove(const Ask& ask) const {
  moonbase::chessengine::BestMoveInput input;
  input.fen = ask.fen;
  if (!ask.moves.empty()) input.moves = ask.moves;
  input.movetimeMs = ask.movetime_ms;
  if (ask.elo.has_value()) input.elo = *ask.elo;
  auto output = client_.BestMove(input);
  if (!output.ok()) return std::move(output).error();
  return std::move(output->uci);
}

}  // namespace chess_engine
