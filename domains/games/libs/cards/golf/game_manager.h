#ifndef CPP_CARDS_GOLF_GAME_MANAGER_H
#define CPP_CARDS_GOLF_GAME_MANAGER_H

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_store.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf {

typedef std::shared_ptr<const GameState> GameStatePtr;
using absl::Status;
using absl::StatusOr;
using std::string;

// Not thread-safe. requires external synchronization
class GameManager {
 public:
  explicit GameManager(std::shared_ptr<GameStoreInterface> game_store)
      : game_store_(std::move(game_store)), dealer_(std::make_shared<Dealer>()) {}
  explicit GameManager(std::shared_ptr<GameStoreInterface> game_store,
                       std::shared_ptr<Dealer> dealer)
      : game_store_(std::move(game_store)), dealer_(std::move(dealer)) {}
  GameManager(GameManager& other) : game_store_(std::move(other.game_store_)) {}
  [[nodiscard]] StatusOr<string> registerUser(const string& user_id);
  void unregisterUser(const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> newGame(const string& user_id, int players);
  [[nodiscard]] StatusOr<GameStatePtr> joinGame(const string& game_id, const string& name);
  [[nodiscard]] StatusOr<GameStatePtr> leaveGame(const string& game_id, const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> peekAtDrawPile(const string& game_id, const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> swapDrawForDiscardPile(const string& game_id,
                                                              const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDrawPile(const string& game_id, const string& user_id,
                                                       Position position);
  [[nodiscard]] StatusOr<GameStatePtr> swapForDiscardPile(const string& game_id,
                                                          const string& user_id, Position position);
  [[nodiscard]] StatusOr<GameStatePtr> knock(const string& game_id, const string& user_id);
  [[nodiscard]] StatusOr<GameStatePtr> getGameStateForUser(const string& game_id,
                                                           const string& user_id) const;

  // do these methods belong here?
  [[nodiscard]] std::unordered_set<string> getUsersOnline() const;
  [[nodiscard]] std::unordered_map<string, string> getGameIdsByUserId() const;
  [[nodiscard]] std::unordered_set<GameStatePtr> getGames() const;
  [[nodiscard]] std::unordered_set<string> getUsersByGameId(const string& game_id) const;

 private:
  [[nodiscard]] StatusOr<GameStatePtr> updateGameState(StatusOr<GameState> update_result,
                                                       const string& game_id);
  [[nodiscard]] std::mt19937 randomGenerator() const;
  [[nodiscard]] string generateRandomAlphanumericString(std::size_t len) const;
  [[nodiscard]] std::optional<string> generateUnusedRandomId() const;
  [[nodiscard]] std::deque<Card> shuffleNewDeck();
  std::shared_ptr<GameStoreInterface> game_store_;
  std::shared_ptr<Dealer> dealer_;
};

}  // namespace golf

#endif
