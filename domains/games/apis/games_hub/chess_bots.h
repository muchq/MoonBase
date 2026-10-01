#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H

#include <charconv>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace games_hub {

/// A chess bot's seat (#1618): Stockfish at a strength, named for both.
/// The `@` keeps it out of the player id space — minted ids are words and
/// dashes — so a seat's id alone says it is a bot, and how strong.
inline constexpr std::string_view kChessBotPrefix = "stockfish@";

inline std::string ChessBotId(int elo) { return std::string(kChessBotPrefix) + std::to_string(elo); }

/// The strength a bot's id names; nothing for a player's.
inline std::optional<int> ChessBotElo(std::string_view player_id) {
  if (!player_id.starts_with(kChessBotPrefix)) return std::nullopt;
  const std::string_view digits = player_id.substr(kChessBotPrefix.size());
  int elo = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), elo);
  if (error != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
  return elo;
}

/// How long a bot thinks over a move, and how long it waits to ask again
/// after the engine failed to answer — its clock running meanwhile.
inline constexpr int kChessBotMovetimeMs = 300;
inline constexpr std::chrono::milliseconds kChessBotRetry{2000};

/// A move asked of the engine: the game's start, its moves since, how
/// long to think, at what strength.
struct ChessBotAsk {
  std::string fen;
  std::vector<std::string> moves;
  int movetime_ms = 0;
  int elo = 0;
};

/// The engine a bot asks, answering a UCI move or why not. Called without
/// the hub's lock, from the bot thread.
using ChessBotEngine = std::function<absl::StatusOr<std::string>(const ChessBotAsk&)>;

}  // namespace games_hub

#endif
