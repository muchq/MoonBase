#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_PONG_BOTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_PONG_BOTS_H

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

#include "domains/games/libs/pong/rally.h"

namespace games_hub {

/// A pong bot's seat: smallcat's own opponent at a level, named for both.
/// The `@` keeps it out of the player id space, as a chess bot's does.
inline constexpr std::string_view kPongBotPrefix = "smallcat@";

inline std::string PongBotId(int level) {
  return std::string(kPongBotPrefix) + std::to_string(level);
}

/// The level a bot's id names; nothing for a player's.
inline std::optional<int> PongBotLevel(std::string_view player_id) {
  if (!player_id.starts_with(kPongBotPrefix)) return std::nullopt;
  const std::string_view digits = player_id.substr(kPongBotPrefix.size());
  int level = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), level);
  if (error != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
  if (level < pong::kMinBotLevel || level > pong::kMaxBotLevel) return std::nullopt;
  return level;
}

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_PONG_BOTS_H
