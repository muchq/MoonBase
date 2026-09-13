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
///
/// Installs an interceptor that sets Accept to PGN. That cannot be modeled:
/// for a blob payload the generated client sets "application/octet-stream"
/// after applying any modeled @httpHeader("Accept"), so the modeled value is
/// written and then overwritten. Worth removing once opal-cpp defaults that
/// header only when unset, the way it already does for document responses.
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

 private:
  explicit Client(moonbase::lichess::LichessClient client) : client_(std::move(client)) {}

  moonbase::lichess::LichessClient client_;
};

}  // namespace lichess

#endif  // DOMAINS_GAMES_LIBS_LICHESS_CPP_CLIENT_H_
