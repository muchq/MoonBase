#ifndef CPP_CARDS_GOLF_GAME_MANAGER_H
#define CPP_CARDS_GOLF_GAME_MANAGER_H

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/game_state.h"
#include "cpp/cards/golf/game_store.h"
#include "cpp/cards/golf/player.h"

namespace golf {

typedef std::shared_ptr<const GameState> GameStatePtr;
using absl::Status;
using absl::StatusOr;
using std::string;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  explicit GameManager(std::shared_ptr<GameStoreInterface> game_store)
      : game_store_(std::move(game_store)) {}
  [[nodiscard]] StatusOr<string> registerUser(const string& user_id);
  void unregisterUser(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> newGame(const string& user_id, int players);
  [[nodiscard]] StatusOr<GameStatePtr> joinGame(const string& game_id, const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> leaveGame(const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> peekAtDrawPile(const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> swapDrawForDiscardPile(const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDrawPile(const string& user_id, Position position);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDiscardPile(const string& user_id, Position position);
  [[nodiscard]] StatusOr<GameStatePtr> knock(const string& user_id);

  // do these methods belong here?
  [[nodiscard]] std::unordered_set<string> getUsersOnline() const;
  [[nodiscard]] std::unordered_map<string, string> getGameIdsByUserId() const;
  [[nodiscard]] std::unordered_set<GameStatePtr> getGames() const;
  [[nodiscard]] std::unordered_set<string> getUsersByGameId(const string& game_id) const;

 private:
  [[nodiscard]] StatusOr<GameStatePtr> getGameStateForUser(const string& user_id) const;
  [[nodiscard]] StatusOr<GameStatePtr> updateGameState(StatusOr<GameState> updateResult,
                                                       const string& gameId);
  [[nodiscard]] std::mt19937 randomGenerator() const;
  [[nodiscard]] string generateRandomAlphanumericString(std::size_t len) const;
  [[nodiscard]] std::optional<string> generateUnusedRandomId() const;
  [[nodiscard]] static std::deque<Card> shuffleNewDeck();
  std::shared_ptr<GameStoreInterface> game_store_;
};

}  // namespace golf

#endif
