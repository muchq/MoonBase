#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H

#include <variant>

#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/rummy/table.h"
#include "domains/games/libs/cards/spades/table.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"
#include "domains/games/libs/pong/table.h"

namespace games_hub {

/// The games a room can host (#79): golf, castle (#77), rummy (#245), chess, pong and
/// spades (#1658). A table is
/// created as one kind and keeps it; the kind names the engine whose
/// state the entry (and its stored row) carries. Its adapter names the
/// kind's word (adapters.h's GameKindName).
enum class GameKind { kGolf, kCastle, kRummy, kChess, kPong, kSpades };

/// The engine truth of a started table, whichever game it plays.
/// A rummy table is a dealer's-choice table (#1609), a deal at a time; a
/// chess table plays one game after another the same way, and so does a
/// pong table, whose state is its score and never its rally, and a spades
/// table, hand after hand and game after game.
using HostedState = std::variant<golf::GameState, castle::GameState, rummy::TableState,
                                 chess_play::Table, pong::Table, spades::Table>;

/// A state's kind is its alternative's index; adapters.h holds every
/// adapter's kKind to it.
inline GameKind KindOf(const HostedState& state) { return static_cast<GameKind>(state.index()); }

inline bool IsOver(const HostedState& state) {
  return std::visit([](const auto& engine) { return engine.isOver(); }, state);
}

}  // namespace games_hub

#endif
