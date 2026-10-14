#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<spades::Table> {
  using Engine = spades::Table;
  using Move = moonbase::games::SpadesMove;
  using Update = moonbase::games::SpadesUpdate;
  using View = moonbase::games::SpadesView;
  static constexpr GameKind kKind = GameKind::kSpades;
  static constexpr std::size_t kMaxSeats = spades::kMaxSeats;
  static constexpr unsigned kCapabilities = 0;

  static constexpr std::string_view kCommandSeries = "spades_commands";
  static constexpr std::array<std::string_view, 7> kCommands = {
      "createGame", "joinGame", "startGame", "leaveGame", "bid", "play", "deal"};
  static constexpr std::string_view kEventSeries = "spades_events";
  static constexpr std::array<std::string_view, 7> kEvents = {
      "gameJoined",  "gameState", "gameCreated", "gameStarted",
      "turnChanged", "gameEnded", "gameLeft"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_spades_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_spades_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const spades::Table& table) {
    return std::string(spades::phaseName(table.phase()));
  }
  static std::string Turn(const spades::Table& table);
  static std::size_t GamesFinished(const spades::Table& table) { return table.games().size(); }
  static StatsDeltas ClosedDeltas(const spades::Table& table,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const spades::Table& before,
                                                  const spades::Table& after);
  static bool DealStarted(const spades::Table& before, const spades::Table& after) {
    return before.phase() == spades::Phase::kWon && after.phase() == spades::Phase::kBidding;
  }
  static absl::StatusOr<spades::Table> WithoutSeat(const spades::Table& table, int seat, int64_t) {
    return table.removePlayer(seat);
  }

  // One seat's table: its own hand, everyone else's as a count.
  static View ViewOf(const TableContext& context, const spades::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const spades::Table& table,
                                                             std::size_t told);
  // Each game's end was its own gameEnded, an abandoned one's among them.
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const spades::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H
