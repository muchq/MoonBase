#ifndef CPP_CARDS_GOLF_GAME_STATE_H
#define CPP_CARDS_GOLF_GAME_STATE_H

#include <deque>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/player.h"

namespace golf {
using namespace cards;

class GameState {
 public:
  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            int _whoseTurn, int _whoKnocked, std::string _gameId)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked),
        gameId(std::move(_gameId)) {}
  [[nodiscard]] bool isOver() const;
  [[nodiscard]] bool allPlayersPresent() const;
  [[nodiscard]] std::unordered_set<int> winners() const;  // winning player indices
  [[nodiscard]] absl::StatusOr<GameState> swapForDrawPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> swapForDiscardPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> knock(int player) const;
  [[nodiscard]] GameState withPlayers(std::vector<Player> newPlayers) const;
  [[nodiscard]] const std::deque<Card>& getDrawPile() const { return drawPile; }
  [[nodiscard]] const std::deque<Card>& getDiscardPile() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] int playerIndex(const std::string& username) const {
    int i = 0;
    for (auto& p : players) {
      if (p.nameMatches(username)) {
        return i;
      }
      i++;
    }
    return -1;
  }
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  [[nodiscard]] int getWhoKnocked() const { return whoKnocked; }
  [[nodiscard]] const std::string& getGameId() const { return gameId; }

 private:
  const std::deque<Card> drawPile;
  const std::deque<Card> discardPile;
  const std::vector<Player> players;
  const int whoseTurn;
  const int whoKnocked;
  const std::string gameId;
};

}  // namespace golf

#endif
