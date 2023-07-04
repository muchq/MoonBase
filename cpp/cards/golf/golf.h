#ifndef CPP_GOLF_SERVICE_GOLF_H
#define CPP_GOLF_SERVICE_GOLF_H

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"

namespace golf {
using namespace cards;

enum class Position { TopLeft, TopRight, BottomLeft, BottomRight };

class Player {
 public:
  Player(std::string _name, Card tl, Card tr, Card bl, Card br)
      : name(_name), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  const int score() const;
  const std::vector<Card> allCards() const;
  const Card cardAt(Position position) const;
  const Player swapCard(Card toSwap, Position position) const;
  bool operator==(const Player& o) const {
    return name == o.name && topLeft == o.topLeft && topRight == o.topRight &&
           bottomLeft == o.bottomLeft && bottomRight == o.bottomRight;
  }

 private:
  const int cardValue(Card c) const;
  const std::string name;
  const Card topLeft;
  const Card topRight;
  const Card bottomLeft;
  const Card bottomRight;
};

class GameState {
 public:
  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            int _whoseTurn, int _whoKnocked)
      : drawPile(_drawPile),
        discardPile(_discardPile),
        players(_players),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked) {}
  const bool isOver() const;
  const std::unordered_set<int> winners() const;  // winning player indices
  const absl::StatusOr<GameState> swapForDrawPile(int player, Position Position) const;
  const absl::StatusOr<GameState> swapForDiscardPile(int player, Position Position) const;
  const absl::StatusOr<GameState> knock(int player) const;
  const std::deque<Card>& getDrawPile() const { return drawPile; }
  const std::deque<Card>& getDiscardPile() const { return discardPile; }
  const std::vector<Player>& getPlayers() const { return players; }
  const int getWhoseTurn() const { return whoseTurn; }
  const int getWhoKnocked() const { return whoKnocked; }

 private:
  const std::deque<Card> drawPile;
  const std::deque<Card> discardPile;
  const std::vector<Player> players;
  const int whoseTurn;
  const int whoKnocked;
};

}  // namespace golf

#endif
