#include "domains/games/apis/games_hub/chess_adapter.h"

#include "domains/games/apis/games_hub/chess_bots.h"
#include "domains/games/apis/games_hub/chess_results.h"
#include "domains/games/apis/games_hub/round_robin_wire.h"

namespace games_hub {
namespace {

// Both seats played, the winner won, a draw credits nobody. A leaver
// played and lost.
StatsDeltas GameDeltas(const chess_play::GameState& state) {
  const std::optional<chess_play::Result>& result = state.result();
  std::string winner;
  if (result.has_value() && result->winner.has_value()) {
    winner = state.players().at(state.seatOf(*result->winner));
  }
  return PlayedWon(state.players(), winner);
}

}  // namespace

// A game in play, one ended with the table waiting on the next, or the
// table closed by a leave.
std::string Adapter<chess_play::Table>::Phase(const chess_play::Table& table) {
  if (table.isOver()) return "closed";
  return table.game().isOver() ? "ended" : "playing";
}

std::string Adapter<chess_play::Table>::Turn(const chess_play::Table& table) {
  const int seat = table.game().whoseTurn();
  return seat < 0 ? "" : table.players().at(seat);
}

// A table closes by a leave: one that ended a game credits it as any end
// does; one between games, nothing — each game was as it ended.
StatsDeltas Adapter<chess_play::Table>::ClosedDeltas(const chess_play::Table& table,
                                                     const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table.game()) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<chess_play::Table>::DealEndDeltas(
    const chess_play::Table& before, const chess_play::Table& after) {
  if (before.game().isOver() || !after.game().isOver() || after.isOver()) return std::nullopt;
  return GameDeltas(after.game());
}

moonbase::games::GameEvents Adapter<chess_play::Table>::Wrap(Update update) {
  moonbase::games::ChessEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromChess(std::move(event));
}

moonbase::games::ChessView Adapter<chess_play::Table>::ViewOf(const TableContext& context,
                                                              const chess_play::Table* table,
                                                              const std::string&) {
  moonbase::games::ChessView view;
  view.gameId = context.game_id;
  if (context.event.has_value()) view.roundRobinId = context.event->event_id;
  view.inCheck = false;
  view.defaultSetupId = chess_play::kDefaultChessSetup;
  for (const chess_play::ChessSetupOption& available : chess_play::AvailableChessSetups()) {
    moonbase::games::ChessSetupOption option;
    option.setupId = available.id;
    option.name = available.name;
    view.availableSetups.push_back(std::move(option));
  }
  if (table == nullptr) {
    view.phase = "waiting";
    if (context.terms.has_value()) view.terms = WireTerms(*context.terms);
    for (const std::string& roster_id : context.roster) {
      moonbase::games::ChessPlayer player;
      player.playerId = roster_id;
      if (ChessBotElo(roster_id).has_value()) player.bot = true;
      view.players.push_back(std::move(player));
    }
    return view;
  }
  const chess_play::GameState& state = table->game();
  const int64_t now = context.now_ms;
  view.phase = Phase(*table);
  view.variant = state.variant();
  view.setupId = state.setupId();
  view.setupName = std::string(*chess_play::ChessSetupName(state.setupId()));
  for (int seat = 0; seat < chess_play::GameState::kSeats; ++seat) {
    moonbase::games::ChessPlayer player;
    player.playerId = state.players().at(seat);
    player.color = std::string(chess_play::ColorName(state.colorOf(seat)));
    if (ChessBotElo(player.playerId).has_value()) player.bot = true;
    view.players.push_back(std::move(player));
  }
  view.fen = state.fen();
  view.moves = state.moves();
  if (!state.isOver()) {
    view.sideToMove = std::string(chess_play::ColorName(state.sideToMove()));
    view.currentPlayerId = state.players().at(state.whoseTurn());
  }
  view.inCheck = state.inCheck();
  view.legalMoves = state.legalMoves();
  moonbase::games::ChessClock clock;
  clock.whiteMs = state.remainingMs(chess_play::Color::kWhite, now);
  clock.blackMs = state.remainingMs(chess_play::Color::kBlack, now);
  clock.initialMs = state.timeControl().initial_ms;
  clock.incrementMs = state.timeControl().increment_ms;
  view.clock = clock;
  if (state.isOver()) view.result = WireChessResult(chess_play::ScoreOf(state));
  for (const chess_play::GameScore& score : table->scoreSheet()) {
    moonbase::games::ChessScoreLine line;
    line.winner = score.winner;
    line.ending = std::string(chess_play::EndingName(score.ending));
    view.scoreSheet.push_back(std::move(line));
  }
  return view;
}

moonbase::games::GameEvents Adapter<chess_play::Table>::StateEvent(View view) {
  moonbase::games::ChessGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<chess_play::Table>::JoinedEvent(View view) {
  moonbase::games::ChessGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Read from the score sheet, so a game that ended before the next began
// is owed all the same.
std::vector<moonbase::games::GameEvents> Adapter<chess_play::Table>::GamesEnded(
    const chess_play::Table& table, std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  for (moonbase::games::ChessResult& result : ChessResultsSince(table, told)) {
    moonbase::games::ChessGameEnded ended;
    ended.result = std::move(result);
    events.push_back(Wrap(Update::FromGameended(std::move(ended))));
  }
  return events;
}

}  // namespace games_hub
