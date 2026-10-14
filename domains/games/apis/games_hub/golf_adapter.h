#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H

#include <unordered_set>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

/// winners() restricted to the seats the roster still names — the
/// forfeit rule. A final state can carry a seat its roster has dropped:
/// an abandonment that ended the game keeps the departed hand so its
/// cards and score reach the scorecard (#1236), and such a seat cannot
/// win — however good its standing score, the game resolved against it.
/// For an ordinary finish the roster names every seat and this is
/// exactly the engine's rule, knocker-takes-ties included.
[[nodiscard]] std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                                   const std::vector<std::string>& roster);

/// Seats are integer indexes; the occupant's identity is the player id.
std::string PlayerIdAt(const golf::GameState& state, int seat);

template <>
struct Adapter<golf::GameState> {
  static constexpr GameKind kKind = GameKind::kGolf;
  static constexpr std::size_t kMaxSeats = 4;
  static std::string Phase(const golf::GameState& state);
  static std::string Turn(const golf::GameState& state);
  static std::size_t GamesFinished(const golf::GameState&) { return 0; }
  static StatsDeltas ClosedDeltas(const golf::GameState& state,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const golf::GameState&, const golf::GameState&) {
    return std::nullopt;
  }
  static bool DealStarted(const golf::GameState&, const golf::GameState&) { return false; }
  static absl::StatusOr<golf::GameState> WithoutSeat(const golf::GameState& state, int seat,
                                                     int64_t) {
    return state.removePlayer(seat);
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H
