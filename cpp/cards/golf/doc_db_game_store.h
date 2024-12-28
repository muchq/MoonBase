#ifndef CPP_CARDS_GOLF_DOC_DB_GAME_STORE_H
#define CPP_CARDS_GOLF_DOC_DB_GAME_STORE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "cpp/cards/golf/game_state.h"
#include "cpp/cards/golf/game_store.h"
#include "cpp/doc_db_client/doc_db_client.h"

namespace golf {

using absl::Status;
using absl::StatusOr;
using doc_db::DocDbClient;
using std::string;
using std::unordered_set;

class DocDbGameStore final : public GameStoreInterface {
 public:
  explicit DocDbGameStore(std::shared_ptr<DocDbClient> client) : client_(std::move(client)) {}

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
  std::shared_ptr<DocDbClient> client_;
};
}  // namespace golf

#endif
