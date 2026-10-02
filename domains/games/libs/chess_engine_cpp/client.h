#ifndef DOMAINS_GAMES_LIBS_CHESS_ENGINE_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_CHESS_ENGINE_CPP_CLIENT_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "moonbase/chessengine/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace chess_engine {

/// How games_hub reaches chess_engine: `endpoint` is the service on the app
/// network (http://chess_engine:8094). One attempt, and a deadline past
/// the service's own (movetime plus 2 s, up to MaxMovetimeMs): a bot that
/// misses it just thinks again on its next turn, its clock running.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// A move to ask for: the position, the moves since, how long to think,
/// and at what strength (nothing: full strength).
struct Ask {
  std::string fen;
  std::vector<std::string> moves;
  int movetime_ms = 0;
  std::optional<int> elo;
};

/// Asks chess_engine for Stockfish's move.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// The move in UCI.
  opal::Outcome<std::string> BestMove(const Ask& ask) const;

 private:
  explicit Client(moonbase::chessengine::ChessEngineClient client) : client_(std::move(client)) {}

  moonbase::chessengine::ChessEngineClient client_;
};

}  // namespace chess_engine

#endif  // DOMAINS_GAMES_LIBS_CHESS_ENGINE_CPP_CLIENT_H_
