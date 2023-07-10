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

typedef std::reference_wrapper<GameState> GameRef;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  [[nodiscard]] absl::StatusOr<std::string> registerUser(const std::string& name);
  void unregisterUser(const std::string& name);
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> newGame(const std::string& name,
                                                                   int players);
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> joinGame(const std::string& gameId,
                                                                    const std::string& name);
  [[nodiscard]] absl::StatusOr<GameRef> leaveGame(const std::string& name);
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> swapForDrawPile(const std::string& name,
                                                                           Position position);
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> swapForDiscardPile(
      const std::string& name, Position position);
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> knock(const std::string& name);

  [[nodiscard]] std::unordered_set<std::string> getUsersOnline() const { return usersOnline; }
  [[nodiscard]] std::unordered_map<std::string, std::string> getGameIdsByUserId() const {
    return gameIdsByUser;
  }
  [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<GameState>>& getGamesById()
      const {
    return gamesById;
  }
  [[nodiscard]] std::unordered_set<std::string> getUsersByGameId(const std::string& gameId) const {
    if (usersByGame.find(gameId) == usersByGame.end()) {
      return {};
    }

    return usersByGame.at(gameId);
  }

 private:
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> getGameStateForUser(
      const std::string& name) const;
  [[nodiscard]] absl::StatusOr<std::shared_ptr<GameState>> updateGameState(
      absl::StatusOr<GameState> updateResult, const std::string& gameId);
  [[nodiscard]] static std::deque<Card> shuffleNewDeck();
  std::unordered_set<std::string> usersOnline;
  std::unordered_map<std::string, std::string> gameIdsByUser;
  std::unordered_map<std::string, std::unordered_set<std::string>> usersByGame;
  std::unordered_map<std::string, std::shared_ptr<GameState>> gamesById;
};

}  // namespace golf

#endif
