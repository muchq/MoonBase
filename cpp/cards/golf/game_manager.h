#ifndef CPP_CARDS_GOLF_GAME_MANAGER_H
#define CPP_CARDS_GOLF_GAME_MANAGER_H

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/game_state.h"
#include "cpp/cards/golf/player.h"

namespace golf {

using absl::StatusOr;
using std::deque;
using std::string;
using std::unordered_map;
using std::unordered_set;

typedef std::shared_ptr<const GameState> GameStatePtr;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  [[nodiscard]] StatusOr<string> registerUser(const string& name);
  void unregisterUser(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> newGame(const string& name, int players);
  [[nodiscard]] StatusOr<GameStatePtr> joinGame(const string& gameId, const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> leaveGame(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> peekAtDrawPile(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> swapDrawForDiscardPile(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDrawPile(const string& name, Position position);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDiscardPile(const string& name, Position position);
  [[nodiscard]] StatusOr<GameStatePtr> knock(const string& name);

  [[nodiscard]] unordered_set<string> getUsersOnline() const { return usersOnline; }
  [[nodiscard]] unordered_map<string, string> getGameIdsByUserId() const { return gameIdsByUser; }
  [[nodiscard]] const unordered_map<string, GameStatePtr>& getGamesById() const {
    return gamesById;
  }
  [[nodiscard]] unordered_set<string> getUsersByGameId(const string& gameId) const {
    if (usersByGame.find(gameId) == usersByGame.end()) {
      return {};
    }
    return usersByGame.at(gameId);
  }

 private:
  [[nodiscard]] StatusOr<GameStatePtr> getGameStateForUser(const string& name) const;
  [[nodiscard]] StatusOr<GameStatePtr> updateGameState(StatusOr<GameState> updateResult,
                                                       const string& gameId);
  [[nodiscard]] static deque<Card> shuffleNewDeck();
  unordered_set<string> usersOnline;
  unordered_map<string, string> gameIdsByUser;
  unordered_map<string, unordered_set<string>> usersByGame;
  unordered_map<string, GameStatePtr> gamesById;
};

}  // namespace golf

#endif
