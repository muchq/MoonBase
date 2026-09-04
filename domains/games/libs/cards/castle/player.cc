#include "domains/games/libs/cards/castle/player.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace castle {
using namespace cards;
using absl::InvalidArgumentError;
using std::vector;

const vector<Card>& Player::row(Source source) const {
  switch (source) {
    case Source::Hand:
      return hand;
    case Source::FaceUp:
      return faceUp;
    case Source::FaceDown:
      return faceDown;
  }
  return faceDown;
}

Source Player::source() const {
  if (!hand.empty()) {
    return Source::Hand;
  }
  if (!faceUp.empty()) {
    return Source::FaceUp;
  }
  return Source::FaceDown;
}

bool Player::isOut() const { return cardsLeft() == 0; }

int Player::cardsLeft() const {
  return static_cast<int>(hand.size() + faceUp.size() + faceDown.size());
}

absl::StatusOr<Player> Player::swapForSetup(int handIndex, int faceUpIndex) const {
  if (handIndex < 0 || handIndex >= static_cast<int>(hand.size())) {
    return InvalidArgumentError("no such hand card");
  }
  if (faceUpIndex < 0 || faceUpIndex >= static_cast<int>(faceUp.size())) {
    return InvalidArgumentError("no such face-up card");
  }
  vector<Card> newHand;
  vector<Card> newFaceUp;
  for (size_t i = 0; i < hand.size(); i++) {
    newHand.push_back(static_cast<int>(i) == handIndex ? faceUp.at(faceUpIndex) : hand.at(i));
  }
  for (size_t i = 0; i < faceUp.size(); i++) {
    newFaceUp.push_back(static_cast<int>(i) == faceUpIndex ? hand.at(handIndex) : faceUp.at(i));
  }
  return Player{id, std::move(newHand), std::move(newFaceUp), faceDown, ready};
}

Player Player::withReady() const { return Player{id, hand, faceUp, faceDown, true}; }

absl::StatusOr<Player> Player::without(Source source, const vector<int>& indexes) const {
  const vector<Card>& from = row(source);
  vector<bool> taken(from.size(), false);
  for (int index : indexes) {
    if (index < 0 || index >= static_cast<int>(from.size())) {
      return InvalidArgumentError("no such card");
    }
    if (taken.at(index)) {
      return InvalidArgumentError("the same card twice");
    }
    taken.at(index) = true;
  }
  vector<Card> kept;
  for (size_t i = 0; i < from.size(); i++) {
    if (!taken.at(i)) {
      kept.push_back(from.at(i));
    }
  }
  switch (source) {
    case Source::Hand:
      return Player{id, std::move(kept), faceUp, faceDown, ready};
    case Source::FaceUp:
      return Player{id, hand, std::move(kept), faceDown, ready};
    case Source::FaceDown:
      return Player{id, hand, faceUp, std::move(kept), ready};
  }
  return InvalidArgumentError("no such row");
}

Player Player::withHandAdded(const vector<Card>& cards) const {
  vector<Card> newHand = hand;
  for (const Card& card : cards) {
    newHand.push_back(card);
  }
  return Player{id, std::move(newHand), faceUp, faceDown, ready};
}

}  // namespace castle
