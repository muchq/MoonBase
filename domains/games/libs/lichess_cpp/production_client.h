#ifndef DOMAINS_GAMES_LIBS_LICHESS_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_GAMES_LIBS_LICHESS_CPP_PRODUCTION_CLIENT_H_

#include <string_view>

#include "domains/games/libs/lichess_cpp/client.h"
#include "opal/core/outcome.h"

namespace lichess {

/// A client against lichess.org, optionally authenticated.
///
/// An empty token builds an anonymous client. That is the correct request to
/// make and currently the one Lichess refuses: the games export answers
/// anonymous callers 404 even for accounts that exist. Building it anyway
/// keeps the token a deployment concern rather than a compile-time one.
opal::Outcome<Client> CreateProductionClient(std::string_view token = "");

}  // namespace lichess

#endif  // DOMAINS_GAMES_LIBS_LICHESS_CPP_PRODUCTION_CLIENT_H_
