#ifndef CPP_GOLF_SERVICE_GOLF_H
#define CPP_GOLF_SERVICE_GOLF_H

#include <deque>
#include <functional>
#include <optional>
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
  Player(std::optional<std::string> _name, Card tl, Card tr, Card bl, Card br)
      : name(_name), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  Player(Card tl, Card tr, Card bl, Card br)
      : name(std::nullopt), topLeft(tl), topRight(tr), bottomLeft(bl), bottomRight(br) {}
  [[nodiscard]] const int score() const;
  [[nodiscard]] const bool isPresent() const;
  [[nodiscard]] const absl::StatusOr<Player> claimHand(std::string username) const;
  [[nodiscard]] const std::vector<Card> allCards() const;
  [[nodiscard]] const Card cardAt(Position position) const;
  [[nodiscard]] const Player swapCard(Card toSwap, Position position) const;
  bool operator==(const Player& o) const {
    return name == o.name && topLeft == o.topLeft && topRight == o.topRight &&
           bottomLeft == o.bottomLeft && bottomRight == o.bottomRight;
  }

 private:
  const int cardValue(Card c) const;
  const std::optional<std::string> name;
  const Card topLeft;
  const Card topRight;
  const Card bottomLeft;
  const Card bottomRight;
};

class GameState {
 public:
  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            int _whoseTurn, int _whoKnocked, std::string _gameId)
      : drawPile(_drawPile),
        discardPile(_discardPile),
        players(_players),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked),
        gameId(_gameId) {}
  [[nodiscard]] const bool isOver() const;
  [[nodiscard]] const bool allPlayersPresent() const;
  [[nodiscard]] const std::unordered_set<int> winners() const;  // winning player indices
  [[nodiscard]] const absl::StatusOr<GameState> swapForDrawPile(int player,
                                                                Position Position) const;
  [[nodiscard]] const absl::StatusOr<GameState> swapForDiscardPile(int player,
                                                                   Position Position) const;
  [[nodiscard]] const absl::StatusOr<GameState> knock(int player) const;
  [[nodiscard]] const std::deque<Card>& getDrawPile() const { return drawPile; }
  [[nodiscard]] const std::deque<Card>& getDiscardPile() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const int getWhoseTurn() const { return whoseTurn; }
  [[nodiscard]] const int getWhoKnocked() const { return whoKnocked; }
  [[nodiscard]] const std::string getGameId() const { return gameId; }

 private:
  const std::deque<Card> drawPile;
  const std::deque<Card> discardPile;
  const std::vector<Player> players;
  const int whoseTurn;
  const int whoKnocked;
  const std::string gameId;
};

typedef std::reference_wrapper<GameState> GameRef;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  [[nodiscard]] const absl::StatusOr<std::string> registerUser(std::string name);
  void unregisterUser(std::string name);
  const absl::StatusOr<GameRef> newGame(std::string userId, int players);
  const absl::StatusOr<GameRef> joinGame(std::string gameId, std::string userId);
  const absl::StatusOr<GameRef> leaveGame(std::string gameId, std::string userId);
  const absl::StatusOr<GameRef> swapForDrawPile(std::string gameId, std::string userId,
                                                Position position);
  const absl::StatusOr<GameRef> swapForDiscardPile(std::string gameId, std::string userId,
                                                   Position position);
  const absl::StatusOr<GameRef> knock(std::string gameId, std::string userId);

  const std::unordered_set<std::string> getUsersOnline() const { return usersOnline; }
  const std::unordered_map<std::string, std::string> getGameIdsByUserId() const {
    return gameIdsByUserId;
  }
  const std::unordered_map<std::string, GameState> getGamesById() const { return gamesById; }

 private:
  std::deque<Card> shuffleNewDeck() const;
  std::unordered_set<std::string> usersOnline;
  std::unordered_map<std::string, std::string> gameIdsByUserId;
  std::unordered_map<std::string, GameState> gamesById;
};

}  // namespace golf

#endif
