#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace games_hub {

/// The games a room can host (#79): golf and castle (#77). A table is
/// created as one kind and keeps it; the kind names the engine whose
/// state the entry (and its stored row) carries, and the wire word on
/// GameSummary.game.
enum class GameKind { kGolf, kCastle };

/// The engine truth of a started table, whichever game it plays.
using HostedState = std::variant<golf::GameState, castle::GameState>;

inline std::string_view GameKindName(GameKind kind) {
  return kind == GameKind::kCastle ? "castle" : "golf";
}

inline std::optional<GameKind> ParseGameKind(std::string_view name) {
  if (name == "golf") return GameKind::kGolf;
  if (name == "castle") return GameKind::kCastle;
  return std::nullopt;
}

inline GameKind KindOf(const HostedState& state) {
  return std::holds_alternative<castle::GameState>(state) ? GameKind::kCastle : GameKind::kGolf;
}

inline bool IsOver(const HostedState& state) {
  return std::visit([](const auto& engine) { return engine.isOver(); }, state);
}

}  // namespace games_hub

#endif
