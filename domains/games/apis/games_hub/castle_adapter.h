#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<castle::GameState> {
  using Engine = castle::GameState;
  using Move = moonbase::games::CastleMove;
  using Update = moonbase::games::CastleUpdate;
  using View = moonbase::games::CastleView;
  static constexpr GameKind kKind = GameKind::kCastle;
  static constexpr std::size_t kMaxSeats = 4;
  static constexpr unsigned kCapabilities = 0;

  static constexpr std::string_view kCommandSeries = "castle_commands";
  static constexpr std::array<std::string_view, 10> kCommands = {
      "createGame", "joinGame",     "startGame",  "leaveGame",    "swapForSetup",
      "ready",      "playFromHand", "playFaceUp", "playFaceDown", "pickUp"};
  static constexpr std::string_view kEventSeries = "castle_events";
  static constexpr std::array<std::string_view, 7> kEvents = {
      "gameJoined",  "gameState", "gameCreated", "gameStarted",
      "turnChanged", "gameEnded", "gameLeft"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_castle_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_castle_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const castle::GameState& state);
  static std::string Turn(const castle::GameState& state);
  static std::size_t GamesFinished(const castle::GameState&) { return 0; }
  static StatsDeltas ClosedDeltas(const castle::GameState& state,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const castle::GameState&,
                                                  const castle::GameState&) {
    return std::nullopt;
  }
  static bool DealStarted(const castle::GameState&, const castle::GameState&) { return false; }
  static absl::StatusOr<castle::GameState> WithoutSeat(const castle::GameState& state, int seat,
                                                       int64_t) {
    return state.removePlayer(seat);
  }

  // Own hand faces only, everyone's once the game ends.
  static View ViewOf(const TableContext& context, const castle::GameState* state,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const castle::GameState&,
                                                             std::size_t) {
    return {};
  }
  static std::optional<moonbase::games::GameEvents> ClosedEvent(
      const castle::GameState& state, const std::vector<std::string>& roster);
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H
