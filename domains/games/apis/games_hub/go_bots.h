#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GO_BOTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GO_BOTS_H

#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "domains/games/libs/go/game_state.h"

namespace games_hub {

/// A go bot's seat: GNU Go at a level, named for both. The `@` keeps it out
/// of the player id space, as chess's does.
inline constexpr std::string_view kGoBotPrefix = "gnugo@";
inline constexpr int kGoBotMinLevel = 1;
inline constexpr int kGoBotMaxLevel = 10;

inline std::string GoBotId(int level) { return std::string(kGoBotPrefix) + std::to_string(level); }

/// The level a bot's id names; nothing for a player's.
inline std::optional<int> GoBotLevel(std::string_view player_id) {
  if (!player_id.starts_with(kGoBotPrefix)) return std::nullopt;
  const std::string_view digits = player_id.substr(kGoBotPrefix.size());
  int level = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), level);
  if (error != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
  if (level < kGoBotMinLevel || level > kGoBotMaxLevel) return std::nullopt;
  return level;
}

/// GTP's columns: A to T, skipping I.
inline constexpr std::string_view kGtpColumns = "ABCDEFGHJKLMNOPQRST";

/// A point (y * size + x, y from the top) as GTP names it: its column, then
/// its row counted from the bottom; go::kPass as "pass".
inline std::string GtpVertex(int point, int size) {
  if (point == go::kPass) return "pass";
  return std::string(1, kGtpColumns[point % size]) + std::to_string(size - point / size);
}

/// The point a GTP vertex names on a `size` board, any case; go::kPass for
/// a pass; nothing for anything else.
inline std::optional<int> GtpPoint(std::string_view vertex, int size) {
  if (absl::EqualsIgnoreCase(vertex, "pass")) return go::kPass;
  if (vertex.size() < 2) return std::nullopt;
  const std::size_t x = kGtpColumns.find(absl::ascii_toupper(vertex.front()));
  if (x == std::string_view::npos || static_cast<int>(x) >= size) return std::nullopt;
  const std::string_view digits = vertex.substr(1);
  int row = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), row);
  if (error != std::errc() || end != digits.data() + digits.size() || row < 1 || row > size) {
    return std::nullopt;
  }
  return (size - row) * size + static_cast<int>(x);
}

/// A move asked of the engine: the board, komi, every move so far as GTP
/// vertices (Black's first, the colours alternating), and the bot's level.
struct GoBotAsk {
  int size = 19;
  double komi = go::kDefaultKomi;
  std::vector<std::string> moves;
  int level = kGoBotMinLevel;
};

/// The engine a bot asks, answering a GTP vertex, "pass" or "resign", or
/// why not. Called without the hub's lock, from the bot thread.
using GoBotEngine = std::function<absl::StatusOr<std::string>(const GoBotAsk&)>;

}  // namespace games_hub

#endif
