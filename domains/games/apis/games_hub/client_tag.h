#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CLIENT_TAG_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CLIENT_TAG_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"

namespace games_hub {

/// Who minted a session, as GetSession's `client` query names it: `web`,
/// or `ios/<version>`. Both labels come from the declared lists below, so
/// every series is declared at zero (#1327, #1384) and nothing a client
/// sends can mint a new one: an unknown client is `other`, an unknown
/// version `other`.
struct ClientTag {
  std::string client;
  std::string version;
  bool operator==(const ClientTag&) const = default;
};

/// The app versions a mint may be counted under. An app release adds its
/// version here; client_tag_test holds the app's Info.plist to it.
inline constexpr std::array<std::string_view, 11> kIosVersions = {
    "0.1.9",  "0.1.10", "0.1.11", "0.1.12", "0.1.13", "0.1.14",
    "0.1.15", "0.1.16", "0.1.17", "0.1.18", "0.1.19"};

ClientTag ClientTagOf(const std::optional<std::string>& sent);

/// hub_session_mints under every tag ClientTagOf can return.
std::vector<CounterSeries> ClientTagSeries();

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CLIENT_TAG_H
