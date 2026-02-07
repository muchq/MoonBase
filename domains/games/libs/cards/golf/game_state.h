#ifndef CPP_CARDS_GOLF_GAME_STATE_H
#define CPP_CARDS_GOLF_GAME_STATE_H

#include <deque>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf {
using namespace cards;
using std::string;

class GameState {
 public:
  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            bool _peekedAtDrawPile, int _whoseTurn, int _whoKnocked)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        peekedAtDrawPile(_peekedAtDrawPile),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked) {}

  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            bool _peekedAtDrawPile, int _whoseTurn, int _whoKnocked, string _gameId,
            string _version_id)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        peekedAtDrawPile(_peekedAtDrawPile),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked),
        gameId(std::move(_gameId)),
        version_id(std::move(_version_id)) {}
  [[nodiscard]] bool isOver() const;
  [[nodiscard]] bool allPlayersPresent() const;
  [[nodiscard]] std::unordered_set<int> winners() const;  // winning player indices
  [[nodiscard]] absl::StatusOr<GameState> peekAtDrawPile(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> swapForDrawPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> swapDrawForDiscardPile(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> swapForDiscardPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> knock(int player) const;
  [[nodiscard]] GameState withPlayers(std::vector<Player> newPlayers) const;
  [[nodiscard]] GameState withIdAndVersion(const string& game_id, const string& version_id) const;
  [[nodiscard]] const std::deque<Card>& getDrawPile() const { return drawPile; }
  [[nodiscard]] const std::deque<Card>& getDiscardPile() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const Player& getPlayer(const int index) const { return players.at(index); }
  [[nodiscard]] int playerIndex(const string& username) const {
    int i = 0;
    for (auto& p : players) {
      if (p.nameMatches(username)) {
        return i;
      }
      i++;
    }
    return -1;
  }
  [[nodiscard]] bool getPeekedAtDrawPile() const { return peekedAtDrawPile; }
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  [[nodiscard]] int getWhoKnocked() const { return whoKnocked; }
  [[nodiscard]] const string& getGameId() const { return gameId; }
  [[nodiscard]] const string& getVersionId() const { return version_id; }

 private:
  const std::deque<Card> drawPile;
  const std::deque<Card> discardPile;
  const std::vector<Player> players;
  const bool peekedAtDrawPile;
  const int whoseTurn;
  const int whoKnocked;
  const std::string gameId;
  const std::string version_id;
};

typedef std::shared_ptr<const GameState> GameStatePtr;

}  // namespace golf

#endif
