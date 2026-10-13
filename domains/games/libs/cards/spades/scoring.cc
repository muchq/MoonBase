#include "domains/games/libs/cards/spades/scoring.h"

#include <algorithm>

namespace spades {

std::vector<TeamScore> scoreHand(const std::vector<int>& bids, const std::vector<int>& tricks,
                                 const std::vector<int>& bags_before) {
  const int teams = static_cast<int>(bags_before.size());
  std::vector<TeamScore> scores(teams);
  for (int team = 0; team < teams; ++team) {
    int contract = 0;
    int counted = 0;
    int points = 0;
    int bags = bags_before[team];
    for (std::size_t seat = team; seat < bids.size(); seat += teams) {
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

std::optional<int> gameWinner(const std::vector<int>& scores) {
  const bool decided = std::any_of(scores.begin(), scores.end(), [](int score) {
    return score >= kWinningScore || score <= kLosingScore;
  });
  if (!decided) return std::nullopt;
  const auto best = std::max_element(scores.begin(), scores.end());
  if (std::count(scores.begin(), scores.end(), *best) > 1) return std::nullopt;
  return static_cast<int>(best - scores.begin());
}

}  // namespace spades
