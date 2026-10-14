#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<rummy::TableState> {
  static constexpr GameKind kKind = GameKind::kRummy;
  static constexpr std::size_t kMaxSeats = 4;
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
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_RUMMY_ADAPTER_H
