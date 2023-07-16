#ifndef CPP_CARDS_GOLF_PLAYER_H
#define CPP_CARDS_GOLF_PLAYER_H

#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"

namespace golf {
using namespace cards;

enum class Position { TopLeft, TopRight, BottomLeft, BottomRight };

class Player {
 public:
  Player(std::optional<std::string> _name, Card tl, Card tr, Card bl, Card br)
      : name(std::move(_name)), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  Player(Card tl, Card tr, Card bl, Card br)
      : name(std::nullopt), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  [[nodiscard]] int score() const;
  [[nodiscard]] const std::optional<std::string>& getName() const;
  [[nodiscard]] bool isPresent() const;
  [[nodiscard]] absl::StatusOr<Player> claimHand(const std::string& username) const;
  [[nodiscard]] std::vector<Card> allCards() const;
  [[nodiscard]] const Card& cardAt(Position position) const;
  [[nodiscard]] Player swapCard(Card toSwap, Position position) const;
  [[nodiscard]] bool nameMatches(const std::string& username) const;
  bool operator==(const Player& o) const {
    return name == o.name && topLeft == o.topLeft && topRight == o.topRight &&
           bottomLeft == o.bottomLeft && bottomRight == o.bottomRight;
  }

 private:
  [[nodiscard]] static int cardValue(Card c);
  const std::optional<std::string> name;
  const Card topLeft;
  const Card topRight;
  const Card bottomLeft;
  const Card bottomRight;
};

}  // namespace golf

#endif
