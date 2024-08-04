#ifndef CPP_CARDS_GOLF_GAME_STORE_H
#define CPP_CARDS_GOLF_GAME_STORE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "cpp/cards/golf/game_state.h"

namespace golf {

typedef std::shared_ptr<const GameState> GameStatePtr;
using absl::Status;
using absl::StatusOr;
using std::string;

class GameStoreInterface {
 public:
  virtual ~GameStoreInterface() {}
  virtual Status AddUser(const string& user_id) = 0;
  virtual bool UserExists(const string& user_id) = 0;
  virtual Status RemoveUser(const string& user_id) = 0;
  virtual StatusOr<std::unordered_set<string>> GetUsers() const = 0;
  virtual StatusOr<GameStatePtr> NewGame(const GameStatePtr game_state) = 0;
  virtual StatusOr<GameStatePtr> ReadGame(const string& game_id) const = 0;
  virtual StatusOr<GameStatePtr> ReadGameByUserId(const string& user_id) const = 0;
  virtual std::unordered_set<GameStatePtr> ReadAllGames() const = 0;
  virtual StatusOr<GameStatePtr> UpdateGame(const GameStatePtr game_state) = 0;
};

class InMemoryGameStore final : public GameStoreInterface {
 public:
  Status AddUser(const string& user_id) override;
  bool UserExists(const string& user_id) override;
  Status RemoveUser(const string& user_id) override;
  StatusOr<std::unordered_set<string>> GetUsers() const override;
  StatusOr<GameStatePtr> NewGame(const GameStatePtr game_state) override;
  StatusOr<GameStatePtr> ReadGame(const string& game_id) const override;
  StatusOr<GameStatePtr> ReadGameByUserId(const string& user_id) const override;
  std::unordered_set<GameStatePtr> ReadAllGames() const override;
  StatusOr<GameStatePtr> UpdateGame(const GameStatePtr game_state) override;

 private:
  std::unordered_set<string> users_online;
  std::unordered_map<string, string> game_ids_by_user_id;
  std::unordered_map<string, GameStatePtr> games_by_id;
};
}  // namespace golf

#endif
