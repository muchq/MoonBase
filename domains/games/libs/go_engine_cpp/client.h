#ifndef DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_CLIENT_H_

#include <string>
#include <utility>
#include <vector>

#include "moonbase/goengine/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace go_engine {

/// How games_hub reaches go_engine: `endpoint` is the service on the app
/// network (http://go_engine:8095). One attempt, and a deadline past the
/// service's own (10 s): a bot that misses it asks again after its retry,
/// its clock running.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// What to ask: the board, komi, every move so far as GTP vertices, the
/// level, and whether it is the dead stones that are asked for.
struct Ask {
  int size = 19;
  double komi = 6.5;
  std::vector<std::string> moves;
  int level = 1;
  bool scoring = false;
};

/// Asks go_engine for GNU Go's answer.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// A vertex, "pass" or "resign"; in the scoring, the dead stones.
  opal::Outcome<std::string> Genmove(const Ask& ask) const;

 private:
  explicit Client(moonbase::goengine::GoEngineClient client) : client_(std::move(client)) {}

  moonbase::goengine::GoEngineClient client_;
};

}  // namespace go_engine

#endif  // DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_CLIENT_H_
