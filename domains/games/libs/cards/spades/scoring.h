#ifndef DOMAINS_GAMES_LIBS_CARDS_SPADES_SCORING_H
#define DOMAINS_GAMES_LIBS_CARDS_SPADES_SCORING_H

#include <optional>
#include <vector>

#include "domains/games/libs/cards/spades/hand.h"

namespace spades {

/// A team that has taken this many bags pays for them.
inline constexpr int kBagLimit = 10;
inline constexpr int kBagPenalty = 100;
inline constexpr int kNilBonus = 100;
/// A team that reaches this at a hand's end may have won the game.
inline constexpr int kWinningScore = 500;
/// A team at or below this at a hand's end may have lost it.
inline constexpr int kLosingScore = -200;

/// One team's hand: what it scored, its bag penalties included, and the
/// bags it carries on.
struct TeamScore {
  int points = 0;
  int bags = 0;
  bool operator==(const TeamScore&) const = default;
};

/// Each team's score for a hand played out, with the bags each carried in.
/// There are as many teams as `bags_before` counts, and seat `s` plays for
/// team `s % teams`: two teams of partners across four seats, or a team
/// of one a seat.
///
///   - A team's contract is its seats' bids that are not nil. Made, it
///     scores 10 a trick bid and 1 a trick over, a bag; set, it loses 10 a
///     trick bid.
///   - A nil scores 100 if its bidder took no trick, and loses 100 if it
///     took any. A nil bidder's tricks never count toward the contract;
///     they are bags.
///   - Every 10 bags carried cost 100 and are cleared.
[[nodiscard]] std::vector<TeamScore> scoreHand(const std::vector<int>& bids,
                                               const std::vector<int>& tricks,
                                               const std::vector<int>& bags_before);

/// The team that has won on these scores, at a hand's end: once any team
/// reaches kWinningScore or falls to kLosingScore, the highest score wins.
/// A tie for the highest plays on.
[[nodiscard]] std::optional<int> gameWinner(const std::vector<int>& scores);

}  // namespace spades

#endif  // DOMAINS_GAMES_LIBS_CARDS_SPADES_SCORING_H
