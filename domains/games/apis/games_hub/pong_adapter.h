#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<pong::Table> {
  static constexpr GameKind kKind = GameKind::kPong;
  static constexpr std::size_t kMaxSeats = pong::kSeats;
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
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
