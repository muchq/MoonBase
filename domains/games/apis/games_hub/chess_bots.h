#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
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

/// How long a bot thinks over a move, from the table's time control.
/// One sixtieth of the initial time plus four fifths of the increment —
/// a rough "moves left in the bank" share — then clamped so a bullet
/// game stays snappy and a long one never burns more than two seconds on
/// one move (well under the client's request timeout). Matches
/// chess_engine's movetime bounds at the edges.
inline constexpr int kChessBotMovetimeFloorMs = 100;
inline constexpr int kChessBotMovetimeCeilMs = 2000;
inline constexpr int kChessBotMovetimeEngineMinMs = 10;  // chess_engine MinMovetimeMs
inline constexpr int64_t kChessBotMovetimeInitialDivisor = 60;
inline constexpr int kChessBotClockReserveMs = 50;

inline int ChessBotMovetimeMs(int64_t initial_ms, int64_t increment_ms) {
  const int64_t from_tc =
      initial_ms / kChessBotMovetimeInitialDivisor + (increment_ms * 4) / 5;
  return static_cast<int>(
      std::clamp(from_tc, int64_t{kChessBotMovetimeFloorMs}, int64_t{kChessBotMovetimeCeilMs}));
}

/// The same budget, capped by what's left on the side to move's clock
/// (less a small reserve for the play round-trip). A near-flag seat still
/// asks for the engine's minimum so the request stays valid; the flag
/// sweep ends the game if the answer arrives late.
inline int ChessBotMovetimeMs(int64_t initial_ms, int64_t increment_ms, int64_t remaining_ms) {
  const int budget = ChessBotMovetimeMs(initial_ms, increment_ms);
  const int64_t usable = remaining_ms - kChessBotClockReserveMs;
  if (usable < kChessBotMovetimeEngineMinMs) return kChessBotMovetimeEngineMinMs;
  return static_cast<int>(std::min(int64_t{budget}, usable));
}

/// How long the bot waits to ask again after the engine failed to answer —
/// its clock running meanwhile.
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
