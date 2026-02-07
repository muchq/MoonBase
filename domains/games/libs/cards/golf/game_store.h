#ifndef CPP_CARDS_GOLF_GAME_STORE_H
#define CPP_CARDS_GOLF_GAME_STORE_H

#include <string>
#include <unordered_set>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace golf {

using absl::Status;
using absl::StatusOr;
using std::string;
using std::unordered_set;

class GameStoreInterface {
 public:
  virtual ~GameStoreInterface() {}
  virtual Status AddUser(const string& user_id) = 0;
  virtual StatusOr<bool> UserExists(const string& user_id) const = 0;
  virtual Status RemoveUser(const string& user_id) = 0;
  virtual StatusOr<std::unordered_set<string>> GetUsers() const = 0;
  virtual StatusOr<GameStatePtr> NewGame(const GameStatePtr game_state) = 0;
  virtual StatusOr<GameStatePtr> ReadGame(const string& game_id) const = 0;
  virtual StatusOr<GameStatePtr> ReadGameByUserId(const string& user_id) const = 0;
  virtual StatusOr<std::unordered_set<GameStatePtr>> ReadAllGames() const = 0;
  virtual StatusOr<GameStatePtr> UpdateGame(const GameStatePtr game_state) = 0;
};
}  // namespace golf

#endif
