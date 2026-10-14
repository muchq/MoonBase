#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<rummy::TableState> {
  using Engine = rummy::TableState;
  using Move = moonbase::games::RummyMove;
  using Update = moonbase::games::RummyUpdate;
  using View = moonbase::games::RummyView;
  struct Start {};
  static constexpr GameKind kKind = GameKind::kRummy;
  static constexpr std::size_t kMaxSeats = 4;
  static constexpr unsigned kCapabilities = 0;
  // Seated, not dealt: each deal is a game, recorded as it is dealt.
  static constexpr bool kOpenStartsAGame = false;

  static constexpr std::string_view kCommandSeries = "rummy_commands";
  static constexpr std::array<std::string_view, 13> kCommands = {
      "createGame",  "joinGame", "startGame", "leaveGame", "chooseVariant", "pass", "drawStock",
      "drawDiscard", "takeDown", "meld",      "layOff",    "discard",       "knock"};
  static constexpr std::string_view kEventSeries = "rummy_events";
  static constexpr std::array<std::string_view, 7> kEvents = {
      "gameJoined",  "gameState", "gameCreated", "gameStarted",
      "turnChanged", "gameEnded", "gameLeft"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_rummy_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_rummy_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const rummy::TableState& table);
  static std::string Turn(const rummy::TableState& table);
  static std::size_t GamesFinished(const rummy::TableState&) { return 0; }
  static StatsDeltas ClosedDeltas(const rummy::TableState& table,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const rummy::TableState& before,
                                                  const rummy::TableState& after);
  static bool DealStarted(const rummy::TableState& before, const rummy::TableState& after) {
    return after.getDealNumber() > before.getDealNumber();
  }
  static absl::StatusOr<rummy::TableState> WithoutSeat(const rummy::TableState& table, int seat,
                                                       int64_t) {
    return table.removePlayer(seat);
  }

  // Own hand faces only while a deal is on, everyone's once it ends.
  static View ViewOf(const TableContext& context, const rummy::TableState* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  // Each deal's own result is its view's lastDeal as it ends.
  static std::vector<moonbase::games::GameEvents> GamesEnded(const rummy::TableState&,
                                                             std::size_t) {
    return {};
  }
  static std::optional<moonbase::games::GameEvents> ClosedEvent(
      const rummy::TableState& table, const std::vector<std::string>& roster);

  static Action<rummy::TableState, Start> Decode(const Move& move, const std::string& player_id);
  // The table opens with its first seat to deal, and the dealer's
  // chooseVariant is each deal's start.
  static absl::StatusOr<rummy::TableState> Open(const OpenContext& context, const Start&,
                                                std::deque<cards::Card>) {
    return rummy::TableState::open(context.game_id, context.roster);
  }
  static std::optional<StartGame<Start>> FilledStart(const std::optional<ChessTerms>&,
                                                     const std::optional<EventTag>&) {
    return std::nullopt;
  }
  static bool IsBot(std::string_view) { return false; }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H
