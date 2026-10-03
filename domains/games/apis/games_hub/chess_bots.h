#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_BOTS_H

#include <algorithm>
#include <array>
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

/// Stockfish's UCI_LimitStrength sandbags so hard that even "2300" will
/// not mate with queen and king. Strength is therefore how long a
/// full-strength engine thinks — the UI Elo labels stay as seat names
/// only. Knots match the UI tiers; values between them interpolate.
inline constexpr int kChessBotMovetimeEngineMinMs = 10;  // chess_engine MinMovetimeMs
inline constexpr int kChessBotClockReserveMs = 50;

inline int ChessBotStrengthThinkMs(int elo) {
  struct Knot {
    int elo;
    int ms;
  };
  // UI tiers: Beginner / Casual / Club / Strong / Full strength.
  constexpr std::array<Knot, 5> kKnots{{
      {1320, 50},
      {1600, 150},
      {1900, 400},
      {2300, 1'200},
      {3190, 4'000},
  }};
  if (elo <= kKnots.front().elo) return kKnots.front().ms;
  if (elo >= kKnots.back().elo) return kKnots.back().ms;
  for (std::size_t i = 1; i < kKnots.size(); ++i) {
    if (elo > kKnots[i].elo) continue;
    const Knot lo = kKnots[i - 1];
    const Knot hi = kKnots[i];
    const int64_t span = hi.elo - lo.elo;
    const int64_t through = elo - lo.elo;
    return static_cast<int>(lo.ms + (through * (hi.ms - lo.ms)) / span);
  }
  return kKnots.back().ms;
}

/// Think time for one move: the strength budget, capped by what's left
/// on the side to move's clock (less a small reserve for the play
/// round-trip). A near-flag seat still asks for the engine's minimum so
/// the request stays valid; the flag sweep ends the game if the answer
/// arrives late.
inline int ChessBotMovetimeMs(int64_t remaining_ms, int elo) {
  const int budget = ChessBotStrengthThinkMs(elo);
  const int64_t usable = remaining_ms - kChessBotClockReserveMs;
  if (usable < kChessBotMovetimeEngineMinMs) return kChessBotMovetimeEngineMinMs;
  return static_cast<int>(std::min(int64_t{budget}, usable));
}

/// How long the bot waits to ask again after the engine failed to answer —
/// its clock running meanwhile.
inline constexpr std::chrono::milliseconds kChessBotRetry{2000};

/// A move asked of the engine: the game's start, its moves since, how
/// long to think. Elo is not sent — Stockfish runs at full strength and
/// the seat's named Elo only sized the think time.
struct ChessBotAsk {
  std::string fen;
  std::vector<std::string> moves;
  int movetime_ms = 0;
  std::optional<int> elo;  // unset: full strength (LimitStrength off)
};

/// The engine a bot asks, answering a UCI move or why not. Called without
/// the hub's lock, from the bot thread.
using ChessBotEngine = std::function<absl::StatusOr<std::string>(const ChessBotAsk&)>;

}  // namespace games_hub

#endif
