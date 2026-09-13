#ifndef DOMAINS_GAMES_LIBS_LICHESS_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_LICHESS_CPP_CLIENT_H_

#include <cstdint>
#include <string_view>
#include <utility>

#include "moonbase/lichess/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace lichess {

/// The Accept that selects concatenated PGN. The documented default, sent
/// explicitly because the format is the contract.
inline constexpr std::string_view kPgnAccept = "application/x-chess-pgn";

/// Production configuration for lichess.org.
///
/// Carries no token. `WithBearerToken` is how one is added, so a caller that
/// has none still builds a client that makes the right anonymous request.
opal::ClientConfig DefaultClientConfig();

/// Returns `config` with `token` on every request as `Authorization: Bearer`.
/// An empty token is left off entirely rather than sent as an empty header,
/// because "no credential" and "an empty credential" are different requests
/// and only the first is the one we mean.
opal::ClientConfig WithBearerToken(opal::ClientConfig config, std::string_view token);

/// Indexer-facing client over the generated Lichess API.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// A player's games over [since_ms, until_ms), as concatenated PGN.
  ///
  /// Milliseconds, as Lichess counts them — chess.com's end_time is seconds,
  /// and the conversion belongs to whoever maps a month onto a range.
  opal::Outcome<moonbase::lichess::ExportGamesOutput> ExportGames(std::string_view username,
                                                                  std::int64_t since_ms,
                                                                  std::int64_t until_ms) const;

  /// Whether this client was built with a token.
  ///
  /// Callers need it because Lichess answers anonymous export calls 404 for
  /// accounts that exist: the status code alone cannot tell "no such player"
  /// from "no credential", and nothing downstream of here knows which.
  bool authenticated() const { return authenticated_; }

 private:
  Client(moonbase::lichess::LichessClient client, bool authenticated)
      : client_(std::move(client)), authenticated_(authenticated) {}

  moonbase::lichess::LichessClient client_;
  bool authenticated_;
};

}  // namespace lichess

#endif  // DOMAINS_GAMES_LIBS_LICHESS_CPP_CLIENT_H_
