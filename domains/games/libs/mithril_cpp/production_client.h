#ifndef DOMAINS_GAMES_LIBS_MITHRIL_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_GAMES_LIBS_MITHRIL_CPP_PRODUCTION_CLIENT_H_

#include <string>

#include "domains/games/libs/mithril_cpp/client.h"

namespace mithril {

/// Creates a client on `endpoint` using the Beast transport.
opal::Outcome<Client> CreateProductionClient(std::string endpoint);

}  // namespace mithril

#endif  // DOMAINS_GAMES_LIBS_MITHRIL_CPP_PRODUCTION_CLIENT_H_
