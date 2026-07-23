#ifndef CPP_CARDS_GOLF_PLAYER_H
#define CPP_CARDS_GOLF_PLAYER_H

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace golf {
using namespace cards;

enum class Position { TopLeft, TopRight, BottomLeft, BottomRight };

/// The wire's 0..3 card indexes in render order.
[[nodiscard]] std::optional<Position> positionFromIndex(int index);
[[nodiscard]] int indexOfPosition(Position position);

class Player {
 public:
  Player(std::optional<std::string> _name, Card tl, Card tr, Card bl, Card br)
      : name(std::move(_name)), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  Player(Card tl, Card tr, Card bl, Card br)
      : name(std::nullopt), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  Player(std::optional<std::string> _name, Card tl, Card tr, Card bl, Card br,
         std::vector<Position> _peeked, bool _donePeeking)
      : name(std::move(_name)),
        topLeft(tl),
        topRight(tr),
        bottomLeft(bl),
        bottomRight(br),
        peeked(std::move(_peeked)),
        donePeeking(_donePeeking) {}
  [[nodiscard]] int score() const;
  [[nodiscard]] const std::optional<std::string>& getName() const;
  [[nodiscard]] bool isPresent() const;
  [[nodiscard]] absl::StatusOr<Player> claimHand(const std::string& username) const;
  [[nodiscard]] std::vector<Card> allCards() const;
  [[nodiscard]] const Card& cardAt(Position position) const;
  [[nodiscard]] Player swapCard(Card toSwap, Position position) const;
  [[nodiscard]] bool nameMatches(const std::string& username) const;

  /// The opening peeks (Go-hub mechanic, #1187 phase 2): each player looks
  /// at two of their own cards before turn play. addPeek enforces the cap
  /// and rejects duplicates; clearPeeks ends the reveal but remembers that
  /// this player is done, so peeks cannot restart mid-game.
  [[nodiscard]] absl::StatusOr<Player> addPeek(Position position) const;
  [[nodiscard]] Player clearPeeks() const;
  [[nodiscard]] const std::vector<Position>& getPeeked() const { return peeked; }
  [[nodiscard]] bool hasCompletedPeeks() const { return donePeeking; }

  bool operator==(const Player& o) const {
    return name == o.name && topLeft == o.topLeft && topRight == o.topRight &&
           bottomLeft == o.bottomLeft && bottomRight == o.bottomRight && peeked == o.peeked &&
           donePeeking == o.donePeeking;
  }

 private:
  [[nodiscard]] static int cardValue(Card c);
  const std::optional<std::string> name;
  const Card topLeft;
  const Card topRight;
  const Card bottomLeft;
  const Card bottomRight;
  const std::vector<Position> peeked;
  const bool donePeeking = false;
};

}  // namespace golf

#endif
