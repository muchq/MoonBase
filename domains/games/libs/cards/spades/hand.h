#ifndef DOMAINS_GAMES_LIBS_CARDS_SPADES_HAND_H
#define DOMAINS_GAMES_LIBS_CARDS_SPADES_HAND_H

#include <array>
#include <deque>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace spades {

using cards::Card;
using cards::Rank;
using cards::Suit;

inline constexpr int kSeats = 4;
inline constexpr int kHandSize = 13;
/// A bid of no tricks at all, scored on its own (scoring.h).
inline constexpr int kNil = 0;
inline constexpr int kMaxBid = kHandSize;
/// turn() once the hand is played out.
inline constexpr int kNoTurn = -1;

/// Seats are partners across the table: 0 and 2 are team 0, 1 and 3 team 1.
[[nodiscard]] constexpr int teamOf(int seat) { return seat % 2; }

enum class Stage { kBidding, kPlaying, kDone };

struct TrickPlay {
  int seat = 0;
  Card card{Suit::Clubs, Rank::Two};
  bool operator==(const TrickPlay&) const = default;
};

/// A finished trick: its four plays from the leader on, and who took it.
struct Trick {
  std::vector<TrickPlay> plays;
  int winner = 0;
  bool operator==(const Trick&) const = default;
};

/// The seat whose card takes these plays, the first of them the lead:
/// the highest spade, or with none the highest of the suit led.
[[nodiscard]] int trickWinner(const std::vector<TrickPlay>& plays);

/// One hand of spades (#1658): thirteen cards a seat, a bid from each,
/// then thirteen tricks.
///
///   - Bidding goes round from the dealer's left; each seat bids 0
///     (nil) to 13 tricks.
///   - The dealer's left leads the first trick; a trick's winner leads
///     the next. A seat follows the suit led if it can.
///   - Spades may not be led until one has been played to a trick
///     (broken), unless the leader holds nothing but spades.
///
/// Refusals: FailedPrecondition for the wrong seat or stage,
/// InvalidArgument for a bid out of range or a play the rules forbid,
/// NotFound for a card the hand does not hold — a play sent against a
/// view the table has moved past.
class Hand {
 public:
  /// Deals a full deck, `shuffled` front first, one card a seat round the
  /// table from the dealer's left.
  [[nodiscard]] static absl::StatusOr<Hand> deal(int dealer, std::deque<Card> shuffled);
  /// The full truth, for a stored row: refused unless play could reach it.
  [[nodiscard]] static absl::StatusOr<Hand> restore(int dealer,
                                                    std::array<std::vector<Card>, kSeats> hands,
                                                    std::array<std::optional<int>, kSeats> bids,
                                                    std::array<int, kSeats> tricks,
                                                    std::vector<TrickPlay> current,
                                                    std::optional<Trick> last, bool spades_broken);

  [[nodiscard]] absl::StatusOr<Hand> bid(int seat, int tricks) const;
  [[nodiscard]] absl::StatusOr<Hand> play(int seat, const Card& card) const;

  /// The cards `seat` may play now; none off turn.
  [[nodiscard]] std::vector<Card> legalPlays(int seat) const;

  [[nodiscard]] Stage stage() const;
  /// The seat to bid or play; kNoTurn once the hand is played out.
  [[nodiscard]] int turn() const;
  [[nodiscard]] int dealer() const { return dealer_; }
  [[nodiscard]] const std::array<std::vector<Card>, kSeats>& hands() const { return hands_; }
  [[nodiscard]] const std::array<std::optional<int>, kSeats>& bids() const { return bids_; }
  /// Tricks taken, seat by seat.
  [[nodiscard]] const std::array<int, kSeats>& tricks() const { return tricks_; }
  /// The trick in play, from its lead.
  [[nodiscard]] const std::vector<TrickPlay>& current() const { return current_; }
  /// The trick last taken; absent before the first.
  [[nodiscard]] const std::optional<Trick>& last() const { return last_; }
  [[nodiscard]] bool spadesBroken() const { return spades_broken_; }

  bool operator==(const Hand&) const = default;

 private:
  Hand(int dealer, std::array<std::vector<Card>, kSeats> hands,
       std::array<std::optional<int>, kSeats> bids, std::array<int, kSeats> tricks,
       std::vector<TrickPlay> current, std::optional<Trick> last, bool spades_broken)
      : dealer_(dealer),
        hands_(std::move(hands)),
        bids_(bids),
        tricks_(tricks),
        current_(std::move(current)),
        last_(std::move(last)),
        spades_broken_(spades_broken) {}

  /// Who leads the trick in play: the last trick's winner, or the
  /// dealer's left for the first.
  [[nodiscard]] int leader() const;
  [[nodiscard]] int tricksPlayed() const;

  int dealer_ = 0;
  std::array<std::vector<Card>, kSeats> hands_;
  std::array<std::optional<int>, kSeats> bids_;
  std::array<int, kSeats> tricks_{};
  std::vector<TrickPlay> current_;
  std::optional<Trick> last_;
  bool spades_broken_ = false;
};

}  // namespace spades

#endif  // DOMAINS_GAMES_LIBS_CARDS_SPADES_HAND_H
