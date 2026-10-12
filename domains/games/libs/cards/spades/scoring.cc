#include "domains/games/libs/cards/spades/scoring.h"

namespace spades {

std::array<TeamScore, kTeams> scoreHand(const std::array<int, kSeats>& bids,
                                        const std::array<int, kSeats>& tricks,
                                        const std::array<int, kTeams>& bags_before) {
  std::array<TeamScore, kTeams> scores;
  for (int team = 0; team < kTeams; ++team) {
    int contract = 0;
    int counted = 0;
    int points = 0;
    int bags = bags_before[team];
    for (int seat = team; seat < kSeats; seat += kTeams) {
      if (bids[seat] == kNil) {
        points += tricks[seat] == 0 ? kNilBonus : -kNilBonus;
        bags += tricks[seat];
      } else {
        contract += bids[seat];
        counted += tricks[seat];
      }
    }
    if (counted >= contract) {
      points += 10 * contract + (counted - contract);
      bags += counted - contract;
    } else {
      points -= 10 * contract;
    }
    while (bags >= kBagLimit) {
      points -= kBagPenalty;
      bags -= kBagLimit;
    }
    scores[team] = {points, bags};
  }
  return scores;
}

std::optional<int> gameWinner(const std::array<int, kTeams>& scores) {
  const bool decided = scores[0] >= kWinningScore || scores[1] >= kWinningScore ||
                       scores[0] <= kLosingScore || scores[1] <= kLosingScore;
  if (!decided || scores[0] == scores[1]) return std::nullopt;
  return scores[0] > scores[1] ? 0 : 1;
}

}  // namespace spades
