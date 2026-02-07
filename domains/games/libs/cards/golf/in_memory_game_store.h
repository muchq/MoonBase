#ifndef CPP_CARDS_GOLF_IN_MEMORY_GAME_STORE_H
#define CPP_CARDS_GOLF_IN_MEMORY_GAME_STORE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_store.h"

namespace golf {

using absl::Status;
using absl::StatusOr;
using std::string;
using std::unordered_set;

class InMemoryGameStore final : public GameStoreInterface {
 public:
  Status AddUser(const string& user_id) override;
  StatusOr<bool> UserExists(const string& user_id) const override;
  Status RemoveUser(const string& user_id) override;
  StatusOr<std::unordered_set<string>> GetUsers() const override;
  StatusOr<GameStatePtr> NewGame(const GameStatePtr game_state) override;
  StatusOr<GameStatePtr> ReadGame(const string& game_id) const override;
  StatusOr<GameStatePtr> ReadGameByUserId(const string& user_id) const override;
  StatusOr<unordered_set<GameStatePtr>> ReadAllGames() const override;
  StatusOr<GameStatePtr> UpdateGame(const GameStatePtr game_state) override;

 private:
  std::unordered_set<string> users_online;
  std::unordered_map<string, string> game_ids_by_user_id;
  std::unordered_map<string, GameStatePtr> games_by_id;
};
}  // namespace golf

#endif
