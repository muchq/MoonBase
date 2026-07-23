#include "domains/games/libs/cards/golf/player.h"

#include <string>
#include <unordered_set>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace golf {
using namespace cards;

std::optional<Position> positionFromIndex(int index) {
  switch (index) {
    case 0:
      return Position::TopLeft;
    case 1:
      return Position::TopRight;
    case 2:
      return Position::BottomLeft;
    case 3:
      return Position::BottomRight;
    default:
      return std::nullopt;
  }
}

int indexOfPosition(Position position) {
  switch (position) {
    case Position::TopLeft:
      return 0;
    case Position::TopRight:
      return 1;
    case Position::BottomLeft:
      return 2;
    case Position::BottomRight:
      return 3;
  }
  return 0;
}

const std::optional<std::string>& Player::getName() const { return name; }

int Player::score() const {
  std::unordered_set<Rank> hand;
  int score = 0;
  for (auto& c : allCards()) {
    if (hand.find(c.getRank()) != hand.end()) {  // pairs cancel each other
      score -= cardValue(c);
      hand.erase(c.getRank());
    } else {
      score += cardValue(c);
      hand.insert(c.getRank());
    }
  }
  return score;
}

bool Player::isPresent() const { return name.has_value(); }

absl::StatusOr<Player> Player::claimHand(const std::string& username) const {
  if (isPresent()) {
    return absl::FailedPreconditionError("already claimed");
  }
  return Player{username, topLeft, topRight, bottomLeft, bottomRight, peeked, donePeeking};
}

std::vector<Card> Player::allCards() const { return {topLeft, topRight, bottomLeft, bottomRight}; }

int Player::cardValue(Card c) {
  switch (c.getRank()) {
    case cards::Rank::Ace:
      return 1;
    case cards::Rank::Two:
      return 2;
    case cards::Rank::Three:
      return 3;
    case cards::Rank::Four:
      return 4;
    case cards::Rank::Five:
      return 5;
    case cards::Rank::Six:
      return 6;
    case cards::Rank::Seven:
      return 7;
    case cards::Rank::Eight:
      return 8;
    case cards::Rank::Nine:
      return 9;
    case cards::Rank::Ten:
      return 10;
    case cards::Rank::Jack:
      return 0;
    case cards::Rank::Queen:
      return 10;
    case cards::Rank::King:
      return 10;
    default:
      return -1;  // error
  }
}

const Card& Player::cardAt(Position position) const {
  if (position == Position::TopLeft) {
    return topLeft;
  } else if (position == Position::TopRight) {
    return topRight;
  } else if (position == Position::BottomLeft) {
    return bottomLeft;
  } else {
    return bottomRight;
  }
}

Player Player::swapCard(Card toSwap, Position position) const {
  if (position == Position::TopLeft) {
    return Player{name, toSwap, topRight, bottomLeft, bottomRight, peeked, donePeeking};
  } else if (position == Position::TopRight) {
    return Player{name, topLeft, toSwap, bottomLeft, bottomRight, peeked, donePeeking};
  } else if (position == Position::BottomLeft) {
    return Player{name, topLeft, topRight, toSwap, bottomRight, peeked, donePeeking};
  } else {
    return Player{name, topLeft, topRight, bottomLeft, toSwap, peeked, donePeeking};
  }
}

absl::StatusOr<Player> Player::addPeek(Position position) const {
  if (donePeeking) {
    return absl::FailedPreconditionError("already peeked at 2 cards");
  }
  for (const Position& seen : peeked) {
    if (seen == position) {
      return absl::FailedPreconditionError("already peeked at this card");
    }
  }
  std::vector<Position> updated{peeked};
  updated.push_back(position);
  const bool done = updated.size() == 2;
  return Player{name, topLeft, topRight, bottomLeft, bottomRight, std::move(updated), done};
}

Player Player::clearPeeks() const {
  return Player{name, topLeft, topRight, bottomLeft, bottomRight, {}, donePeeking};
}

bool Player::nameMatches(const std::string& username) const { return name == username; }

}  // namespace golf
