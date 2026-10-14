#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<chess_play::Table> {
  using Engine = chess_play::Table;
  using Move = moonbase::games::ChessMove;
  using Update = moonbase::games::ChessUpdate;
  using View = moonbase::games::ChessView;
  static constexpr GameKind kKind = GameKind::kChess;
  static constexpr std::size_t kMaxSeats = chess_play::GameState::kSeats;
  static constexpr unsigned kCapabilities = kWatchable;

  static constexpr std::string_view kCommandSeries = "chess_commands";
  static constexpr std::array<std::string_view, 17> kCommands = {
      "createGame",       "joinGame", "startGame", "leaveGame",   "play",          "resign",
      "addBot",           "watch",    "history",   "review",      "publish",       "challenge",
      "createRoundRobin", "withdraw", "forfeit",   "roundRobins", "playRoundRobin"};
  static constexpr std::string_view kEventSeries = "chess_events";
  static constexpr std::array<std::string_view, 12> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "turnChanged", "gameEnded",
      "gameLeft",   "history",   "review",      "published",   "roundRobin",  "roundRobins"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_chess_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_chess_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const chess_play::Table& table);
  static std::string Turn(const chess_play::Table& table);
  static std::size_t GamesFinished(const chess_play::Table& table) {
    return table.scoreSheet().size();
  }
  static StatsDeltas ClosedDeltas(const chess_play::Table& table,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const chess_play::Table& before,
                                                  const chess_play::Table& after);
  static bool DealStarted(const chess_play::Table& before, const chess_play::Table& after) {
    return before.game().isOver() && !after.game().isOver();
  }
  // The leave reads the clock first: a flag already fallen ends the game
  // on time, not by the leave.
  static absl::StatusOr<chess_play::Table> WithoutSeat(const chess_play::Table& table, int seat,
                                                       int64_t now_ms) {
    return table.removePlayer(seat, now_ms);
  }

  // Chess hides nothing: every seat and watcher sees the one view.
  static View ViewOf(const TableContext& context, const chess_play::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const chess_play::Table& table,
                                                             std::size_t told);
  // Each game's result was its own gameEnded, the close's among them.
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const chess_play::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
