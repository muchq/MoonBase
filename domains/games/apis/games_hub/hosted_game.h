#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HOSTED_GAME_H

#include <string>
#include <variant>

#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/rummy/table.h"
#include "domains/games/libs/cards/spades/table.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"
#include "domains/games/libs/go/table.h"
#include "domains/games/libs/pong/table.h"

namespace games_hub {

/// The games a room can host: golf, castle, rummy, chess, pong, spades
/// and go. A table is
/// created as one kind and keeps it; the kind names the engine whose
/// state the entry (and its stored row) carries. Its adapter names the
/// kind's word (adapters.h's GameKindName).
enum class GameKind { kGolf, kCastle, kRummy, kChess, kPong, kSpades, kGo };

/// The engine truth of a started table, whichever game it plays.
/// A rummy table is a dealer's-choice table, a deal at a time; a
/// chess table plays one game after another the same way, and so does a
/// pong table, whose state is its score and never its rally, and a spades
/// table, hand after hand and game after game, and a go table.
using HostedState = std::variant<golf::GameState, castle::GameState, rummy::TableState,
                                 chess_play::Table, pong::Table, spades::Table, go::Table>;

/// A state's kind is its alternative's index; adapters.h holds every
/// adapter's kKind to it.
inline GameKind KindOf(const HostedState& state) { return static_cast<GameKind>(state.index()); }

inline bool IsOver(const HostedState& state) {
  return std::visit([](const auto& engine) { return engine.isOver(); }, state);
}

/// A chess challenge's or round robin's terms: the setup and clock its
/// games start on.
struct ChessTerms {
  std::string setup_id;
  chess_play::TimeControl time_control;
  bool operator==(const ChessTerms&) const = default;
};

/// A pong round robin's terms: the variant its pairings play.
struct PongTerms {
  pong::Variant variant = pong::Variant::kSmallcat;
  bool operator==(const PongTerms&) const = default;
};

/// A go challenge's terms: the board, komi and clock its game starts on;
/// absent fields at the post take these defaults, 19 by 19, komi 6.5, ten
/// minutes and five seconds.
struct GoTerms {
  int size = 19;
  double komi = go::kDefaultKomi;
  go::TimeControl time_control{600'000, 5'000};
  bool operator==(const GoTerms&) const = default;
};

/// What a game's challenge or round robin plays on, in that game's own
/// shape. The hub carries it without reading it: each alternative is one
/// adapter's Terms, and only that adapter reads or stores it (adapters.h
/// holds them to that).
using HostedTerms = std::variant<ChessTerms, PongTerms, GoTerms>;

}  // namespace games_hub

#endif
