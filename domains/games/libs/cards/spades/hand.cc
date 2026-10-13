#include "domains/games/libs/cards/spades/hand.h"

#include <algorithm>
#include <set>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace spades {
namespace {

constexpr std::size_t kDeckSize = 52;

bool ValidSeat(int seat, int seats) { return seat >= 0 && seat < seats; }

bool HoldsSuit(const std::vector<Card>& hand, Suit suit) {
  return std::any_of(hand.begin(), hand.end(),
                     [suit](const Card& card) { return card.getSuit() == suit; });
}

}  // namespace

int trickWinner(const std::vector<TrickPlay>& plays) {
  const Suit led = plays.front().card.getSuit();
  const auto beats = [led](const Card& challenger, const Card& best) {
    const bool challenger_counts =
        challenger.getSuit() == Suit::Spades || challenger.getSuit() == led;
    if (!challenger_counts) return false;
    if (challenger.getSuit() == best.getSuit()) return challenger.getRank() > best.getRank();
    return challenger.getSuit() == Suit::Spades;
  };
  std::size_t best = 0;
  for (std::size_t i = 1; i < plays.size(); ++i) {
    if (beats(plays[i].card, plays[best].card)) best = i;
  }
  return plays[best].seat;
}

absl::StatusOr<Hand> Hand::deal(int seats, int dealer, std::deque<Card> shuffled) {
  if (seats < kMinSeats || seats > kMaxSeats) {
    return absl::InvalidArgumentError(absl::StrCat("spades seats ", kMinSeats, " to ", kMaxSeats));
  }
  if (!ValidSeat(dealer, seats)) return absl::InvalidArgumentError("no such dealer");
  if (shuffled.size() != kDeckSize) {
    return absl::InvalidArgumentError(absl::StrCat("a deal takes ", kDeckSize, " cards"));
  }
  std::set<int> seen;
  for (const Card& card : shuffled) {
    if (!seen.insert(card.intValue()).second) {
      return absl::InvalidArgumentError("a deal takes every card once");
    }
  }
  if (seats == 3) {
    std::erase(shuffled, Card(Suit::Clubs, Rank::Two));
  }
  std::vector<std::vector<Card>> hands(seats);
  for (int i = 0; i < seats * handSize(seats); ++i) {
    hands[(dealer + 1 + i) % seats].push_back(shuffled[i]);
  }
  return Hand(dealer, std::move(hands), std::vector<std::optional<int>>(seats),
              std::vector<int>(seats), {}, std::nullopt, false);
}

absl::StatusOr<Hand> Hand::restore(int dealer, std::vector<std::vector<Card>> hands,
                                   std::vector<std::optional<int>> bids, std::vector<int> tricks,
                                   std::vector<TrickPlay> current, std::optional<Trick> last,
                                   bool spades_broken) {
  const int seat_count = static_cast<int>(hands.size());
  if (seat_count < kMinSeats || seat_count > kMaxSeats) {
    return absl::InvalidArgumentError(absl::StrCat("spades seats ", kMinSeats, " to ", kMaxSeats));
  }
  if (bids.size() != hands.size() || tricks.size() != hands.size()) {
    return absl::InvalidArgumentError("a bid and a trick count a seat");
  }
  const int hand_size = handSize(seat_count);
  if (!ValidSeat(dealer, seat_count)) return absl::InvalidArgumentError("no such dealer");
  // Bids go round from the dealer's left: those made are a run from there.
  int bids_made = 0;
  for (int i = 0; i < seat_count; ++i) {
    const auto& made = bids[(dealer + 1 + i) % seat_count];
    if (!made.has_value()) break;
    if (*made < kNil || *made > hand_size) return absl::InvalidArgumentError("a bid out of range");
    ++bids_made;
  }
  for (int i = bids_made; i < seat_count; ++i) {
    if (bids[(dealer + 1 + i) % seat_count].has_value()) {
      return absl::InvalidArgumentError("a bid out of turn");
    }
  }
  int played = 0;
  for (int taken : tricks) {
    if (taken < 0) return absl::InvalidArgumentError("tricks taken below zero");
    played += taken;
  }
  if (played > hand_size || current.size() >= hands.size()) {
    return absl::InvalidArgumentError("more tricks than a hand holds");
  }
  if (bids_made < seat_count && (played > 0 || !current.empty())) {
    return absl::InvalidArgumentError("a card played before the bidding ended");
  }
  if (played == 0 && last.has_value()) return absl::InvalidArgumentError("a last trick unplayed");
  if (played > 0) {
    if (!last.has_value() || last->plays.size() != hands.size() ||
        !ValidSeat(last->winner, seat_count) || trickWinner(last->plays) != last->winner ||
        tricks[last->winner] == 0) {
      return absl::InvalidArgumentError("the last trick is not one play could leave");
    }
    if (!ValidSeat(last->plays.front().seat, seat_count)) {
      return absl::InvalidArgumentError("the last trick is played by no seat");
    }
    for (int i = 0; i < seat_count; ++i) {
      if (last->plays[i].seat != (last->plays.front().seat + i) % seat_count) {
        return absl::InvalidArgumentError("the last trick is played out of turn");
      }
    }
  }
  const int leader = last.has_value() ? last->winner : (dealer + 1) % seat_count;
  std::vector<bool> played_now(seat_count);
  for (std::size_t i = 0; i < current.size(); ++i) {
    if (current[i].seat != (leader + static_cast<int>(i)) % seat_count) {
      return absl::InvalidArgumentError("the trick in play is played out of turn");
    }
    played_now[current[i].seat] = true;
  }
  std::set<int> seen;
  // Three seats deal without the two of clubs.
  const Card dropped(Suit::Clubs, Rank::Two);
  const auto once = [&seen, &dropped, seat_count](const Card& card) {
    return !(seat_count == 3 && card == dropped) && seen.insert(card.intValue()).second;
  };
  for (int seat = 0; seat < seat_count; ++seat) {
    const std::size_t holds =
        static_cast<std::size_t>(hand_size - played) - (played_now[seat] ? 1 : 0);
    if (hands[seat].size() != holds) {
      return absl::InvalidArgumentError(absl::StrCat("seat ", seat, " holds the wrong count"));
    }
    for (const Card& card : hands[seat]) {
      if (!once(card)) return absl::InvalidArgumentError("a card twice");
    }
  }
  bool spade_seen = false;
  for (const TrickPlay& play : current) {
    if (!once(play.card)) return absl::InvalidArgumentError("a card twice");
    spade_seen = spade_seen || play.card.getSuit() == Suit::Spades;
  }
  if (last.has_value()) {
    for (const TrickPlay& play : last->plays) {
      if (!once(play.card)) return absl::InvalidArgumentError("a card twice");
      spade_seen = spade_seen || play.card.getSuit() == Suit::Spades;
    }
  }
  if (spade_seen && !spades_broken) {
    return absl::InvalidArgumentError("a spade played with spades unbroken");
  }
  if (spades_broken && played == 0 && !spade_seen) {
    return absl::InvalidArgumentError("spades broken before any was played");
  }
  // A seat off the suit led held none of it, and holds none now: a hand
  // only loses cards.
  const auto followed = [&hands](const std::vector<TrickPlay>& plays) {
    if (plays.empty()) return true;
    const Suit led = plays.front().card.getSuit();
    return std::all_of(plays.begin() + 1, plays.end(), [&](const TrickPlay& play) {
      return play.card.getSuit() == led || !HoldsSuit(hands[play.seat], led);
    });
  };
  if (!followed(current) || (last.has_value() && !followed(last->plays))) {
    return absl::InvalidArgumentError("a card off the suit led by a seat that holds it");
  }
  return Hand(dealer, std::move(hands), bids, tricks, std::move(current), std::move(last),
              spades_broken);
}

int Hand::tricksPlayed() const {
  int played = 0;
  for (int taken : tricks_) played += taken;
  return played;
}

int Hand::leader() const { return last_.has_value() ? last_->winner : (dealer_ + 1) % seats(); }

Stage Hand::stage() const {
  if (std::any_of(bids_.begin(), bids_.end(), [](const auto& bid) { return !bid.has_value(); })) {
    return Stage::kBidding;
  }
  return tricksPlayed() == handSize(seats()) ? Stage::kDone : Stage::kPlaying;
}

int Hand::turn() const {
  switch (stage()) {
    case Stage::kBidding:
      for (int i = 0; i < seats(); ++i) {
        const int seat = (dealer_ + 1 + i) % seats();
        if (!bids_[seat].has_value()) return seat;
      }
      return kNoTurn;
    case Stage::kPlaying:
      return (leader() + static_cast<int>(current_.size())) % seats();
    case Stage::kDone:
      break;
  }
  return kNoTurn;
}

absl::StatusOr<Hand> Hand::bid(int seat, int tricks) const {
  if (stage() != Stage::kBidding) return absl::FailedPreconditionError("the bidding is over");
  if (seat != turn()) return absl::FailedPreconditionError("not your bid");
  if (tricks < kNil || tricks > handSize(seats())) {
    return absl::InvalidArgumentError(absl::StrCat("a bid is 0 (nil) to ", handSize(seats())));
  }
  auto bids = bids_;
  bids[seat] = tricks;
  return Hand(dealer_, hands_, bids, tricks_, current_, last_, spades_broken_);
}

std::vector<Card> Hand::legalPlays(int seat) const {
  if (stage() != Stage::kPlaying || seat != turn()) return {};
  const std::vector<Card>& hand = hands_[seat];
  std::vector<Card> legal;
  if (current_.empty()) {
    // A lead: anything but a spade until they are broken, unless the hand
    // is all spades.
    const bool only_spades = !std::any_of(
        hand.begin(), hand.end(), [](const Card& card) { return card.getSuit() != Suit::Spades; });
    for (const Card& card : hand) {
      if (spades_broken_ || only_spades || card.getSuit() != Suit::Spades) legal.push_back(card);
    }
    return legal;
  }
  const Suit led = current_.front().card.getSuit();
  if (!HoldsSuit(hand, led)) return hand;
  for (const Card& card : hand) {
    if (card.getSuit() == led) legal.push_back(card);
  }
  return legal;
}

absl::StatusOr<Hand> Hand::play(int seat, const Card& card) const {
  if (stage() != Stage::kPlaying) return absl::FailedPreconditionError("no trick in play");
  if (seat != turn()) return absl::FailedPreconditionError("not your turn");
  const std::vector<Card>& held = hands_[seat];
  const auto at = std::find(held.begin(), held.end(), card);
  if (at == held.end()) return absl::NotFoundError("that card is not in your hand");
  const std::vector<Card> legal = legalPlays(seat);
  if (std::find(legal.begin(), legal.end(), card) == legal.end()) {
    return absl::InvalidArgumentError(current_.empty() ? "spades are not broken"
                                                       : "follow the suit led");
  }
  auto hands = hands_;
  hands[seat].erase(hands[seat].begin() + (at - held.begin()));
  auto current = current_;
  current.push_back({seat, card});
  const bool broken = spades_broken_ || card.getSuit() == Suit::Spades;
  if (current.size() < hands_.size()) {
    return Hand(dealer_, std::move(hands), bids_, tricks_, std::move(current), last_, broken);
  }
  const int winner = trickWinner(current);
  auto tricks = tricks_;
  ++tricks[winner];
  return Hand(dealer_, std::move(hands), bids_, tricks, {}, Trick{std::move(current), winner},
              broken);
}

}  // namespace spades
