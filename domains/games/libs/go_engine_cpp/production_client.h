#ifndef DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_PRODUCTION_CLIENT_H_

#include <string>

#include "domains/games/libs/go_engine_cpp/client.h"

namespace go_engine {

/// Creates a client on `endpoint` using the Beast transport.
opal::Outcome<Client> CreateProductionClient(std::string endpoint);

}  // namespace go_engine

#endif  // DOMAINS_GAMES_LIBS_GO_ENGINE_CPP_PRODUCTION_CLIENT_H_
