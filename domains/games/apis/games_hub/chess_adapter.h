#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

template <>
struct Adapter<chess_play::Table> {
  static constexpr GameKind kKind = GameKind::kChess;
  static constexpr std::size_t kMaxSeats = chess_play::GameState::kSeats;
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
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
