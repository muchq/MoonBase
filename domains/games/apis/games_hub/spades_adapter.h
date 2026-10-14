#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<spades::Table> {
  static constexpr GameKind kKind = GameKind::kSpades;
  static constexpr std::size_t kMaxSeats = spades::kMaxSeats;
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
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_SPADES_ADAPTER_H
