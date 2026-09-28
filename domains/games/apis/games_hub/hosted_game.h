#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/rummy/game_state.h"

namespace games_hub {

/// The games a room can host (#79): golf, castle (#77) and rummy (#245). A table is
/// created as one kind and keeps it; the kind names the engine whose
/// state the entry (and its stored row) carries, and the wire word on
/// GameSummary.game.
enum class GameKind { kGolf, kCastle, kRummy };

/// The engine truth of a started table, whichever game it plays.
using HostedState = std::variant<golf::GameState, castle::GameState, rummy::GameState>;

inline std::string_view GameKindName(GameKind kind) {
  switch (kind) {
    case GameKind::kCastle:
      return "castle";
    case GameKind::kRummy:
      return "rummy";
    case GameKind::kGolf:
      break;
  }
  return "golf";
}

inline std::optional<GameKind> ParseGameKind(std::string_view name) {
  if (name == "golf") return GameKind::kGolf;
  if (name == "castle") return GameKind::kCastle;
  if (name == "rummy") return GameKind::kRummy;
  return std::nullopt;
}

inline GameKind KindOf(const HostedState& state) {
  if (std::holds_alternative<castle::GameState>(state)) return GameKind::kCastle;
  if (std::holds_alternative<rummy::GameState>(state)) return GameKind::kRummy;
  return GameKind::kGolf;
}

inline bool IsOver(const HostedState& state) {
  return std::visit([](const auto& engine) { return engine.isOver(); }, state);
}

}  // namespace games_hub

#endif
