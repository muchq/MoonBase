#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_TURN_CREDENTIALS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_TURN_CREDENTIALS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "moonbase/games/types.h"

namespace games_hub {

/// The TURN server a voice roster hands each joiner (#1590), and the
/// secret it shares with the hub. Credentials are minted once, at join,
/// and coturn checks them on every refresh, so `ttl` bounds a call.
struct TurnConfig {
  std::vector<std::string> urls;
  std::string secret;
  absl::Duration ttl = absl::Hours(24);
};

/// TURN_URLS (comma-separated) and TURN_SECRET as a config, each trimmed;
/// nullopt unless both name something, so a half-configured TURN is no TURN.
std::optional<TurnConfig> TurnConfigFrom(std::string_view urls, std::string_view secret);

/// One joiner's TURN server, with credentials coturn's use-auth-secret
/// accepts until the first hour boundary past `now + ttl`: username
/// "<expiry unix seconds>:<playerId>", credential
/// base64(HMAC-SHA1(secret, username)). Rounding keeps a player's username
/// for the hour, so rejoining doesn't reset coturn's per-user quota.
moonbase::games::IceServer TurnServerFor(const TurnConfig& config, const std::string& player_id,
                                         absl::Time now);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_TURN_CREDENTIALS_H
