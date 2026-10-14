#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

/// Castle's phase word: setup, playing or ended.
std::string CastlePhaseString(const castle::GameState& state);

/// The occupant at `seat`, or "" for no seat.
std::string CastlePlayerIdAt(const castle::GameState& state, int seat);

template <>
struct Adapter<castle::GameState> {
  static constexpr GameKind kKind = GameKind::kCastle;
  static constexpr std::size_t kMaxSeats = 4;
  static std::string Phase(const castle::GameState& state) { return CastlePhaseString(state); }
  static std::string Turn(const castle::GameState& state) {
    return CastlePlayerIdAt(state, state.getWhoseTurn());
  }
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
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CASTLE_ADAPTER_H
