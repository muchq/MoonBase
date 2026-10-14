#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<pong::Table> {
  using Engine = pong::Table;
  using Move = moonbase::games::PongMove;
  using Update = moonbase::games::PongUpdate;
  using View = moonbase::games::PongView;
  static constexpr GameKind kKind = GameKind::kPong;
  static constexpr std::size_t kMaxSeats = pong::kSeats;
  static constexpr unsigned kCapabilities = kWatchable;

  static constexpr std::string_view kCommandSeries = "pong_commands";
  static constexpr std::array<std::string_view, 12> kCommands = {
      "createGame", "joinGame",         "startGame", "leaveGame", "paddle",      "addBot",
      "watch",      "createRoundRobin", "withdraw",  "forfeit",   "roundRobins", "playRoundRobin"};
  static constexpr std::string_view kEventSeries = "pong_events";
  static constexpr std::array<std::string_view, 9> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "gameEnded",
      "gameLeft",   "frame",     "roundRobin",  "roundRobins"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_pong_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_pong_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const pong::Table& table);
  // Pong has no turns: both seats play at once.
  static std::string Turn(const pong::Table&) { return ""; }
  static std::size_t GamesFinished(const pong::Table& table) { return table.scoreSheet().size(); }
  static StatsDeltas ClosedDeltas(const pong::Table& table, const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const pong::Table& before,
                                                  const pong::Table& after);
  static bool DealStarted(const pong::Table& before, const pong::Table& after) {
    return !before.inPlay() && after.inPlay();
  }
  static absl::StatusOr<pong::Table> WithoutSeat(const pong::Table& table, int seat, int64_t) {
    return table.removePlayer(seat);
  }

  // Pong hides nothing either: every seat and watcher sees the one view.
  static View ViewOf(const TableContext& context, const pong::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const pong::Table& table,
                                                             std::size_t told);
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const pong::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
