#ifndef DOMAINS_GAMES_LIBS_ONE_D4_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_GAMES_LIBS_ONE_D4_CPP_PRODUCTION_CLIENT_H_

#include <string>

#include "domains/games/libs/one_d4_cpp/client.h"

namespace one_d4 {

/// Creates a client on `endpoint` using the Beast transport.
opal::Outcome<Client> CreateProductionClient(std::string endpoint);

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_CPP_PRODUCTION_CLIENT_H_
