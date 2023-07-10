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

typedef std::shared_ptr<const GameState> GameStatePtr;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  [[nodiscard]] absl::StatusOr<std::string> registerUser(const std::string& name);
  void unregisterUser(const std::string& name);
  [[nodiscard]] absl::StatusOr<GameStatePtr> newGame(const std::string& name, int players);
  [[nodiscard]] absl::StatusOr<GameStatePtr> joinGame(const std::string& gameId,
                                                      const std::string& name);
  [[nodiscard]] absl::StatusOr<GameStatePtr> leaveGame(const std::string& name);
  [[nodiscard]] absl::StatusOr<GameStatePtr> peekAtDrawPile(const std::string& name);
  [[nodiscard]] absl::StatusOr<GameStatePtr> swapDrawForDiscardPile(const std::string& name);
  [[nodiscard]] absl::StatusOr<GameStatePtr> swapForDrawPile(const std::string& name,
                                                             Position position);
  [[nodiscard]] absl::StatusOr<GameStatePtr> swapForDiscardPile(const std::string& name,
                                                                Position position);
  [[nodiscard]] absl::StatusOr<GameStatePtr> knock(const std::string& name);

  [[nodiscard]] std::unordered_set<std::string> getUsersOnline() const { return usersOnline; }
  [[nodiscard]] std::unordered_map<std::string, std::string> getGameIdsByUserId() const {
    return gameIdsByUser;
  }
  [[nodiscard]] const std::unordered_map<std::string, GameStatePtr>& getGamesById() const {
    return gamesById;
  }
  [[nodiscard]] std::unordered_set<std::string> getUsersByGameId(const std::string& gameId) const {
    if (usersByGame.find(gameId) == usersByGame.end()) {
      return {};
    }

    return usersByGame.at(gameId);
  }

 private:
  [[nodiscard]] absl::StatusOr<GameStatePtr> getGameStateForUser(const std::string& name) const;
  [[nodiscard]] absl::StatusOr<GameStatePtr> updateGameState(absl::StatusOr<GameState> updateResult,
                                                             const std::string& gameId);
  [[nodiscard]] static std::deque<Card> shuffleNewDeck();
  std::unordered_set<std::string> usersOnline;
  std::unordered_map<std::string, std::string> gameIdsByUser;
  std::unordered_map<std::string, std::unordered_set<std::string>> usersByGame;
  std::unordered_map<std::string, GameStatePtr> gamesById;
};

}  // namespace golf

#endif
