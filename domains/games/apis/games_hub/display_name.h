#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_DISPLAY_NAME_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_DISPLAY_NAME_H

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace games_hub {

/// The most characters (code points) a display name holds.
inline constexpr int kMaxDisplayNameChars = 24;

/// A player's chosen name as the hub keeps it: trimmed of surrounding
/// whitespace, empty to clear it. Refused when it is longer than
/// kMaxDisplayNameChars, is not printable UTF-8, or is a chat bot's name
/// in any case. Names are labels: two players may share one, and the
/// player id stays the identity.
absl::StatusOr<std::string> NormalizeDisplayName(std::string_view name);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_DISPLAY_NAME_H
