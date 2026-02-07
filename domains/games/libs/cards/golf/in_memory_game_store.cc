#include "domains/games/libs/cards/golf/in_memory_game_store.h"

#include <mutex>
#include <ranges>
#include <string>
#include <unordered_set>

#include "absl/status/statusor.h"

namespace golf {
using absl::Status;
using absl::StatusOr;
using std::string;
using std::unordered_set;

std::mutex users_mutex{};
std::mutex game_state_mutex{};

static int counter = 0;

Status InMemoryGameStore::AddUser(const string& user_id) {
  std::scoped_lock lock{users_mutex};
  if (users_online.contains(user_id)) {
    return absl::AlreadyExistsError("already exists");
  }

  users_online.insert(user_id);
  return absl::OkStatus();
}

StatusOr<bool> InMemoryGameStore::UserExists(const string& user_id) const {
  std::scoped_lock lock{users_mutex};
  return users_online.contains(user_id);
}

Status InMemoryGameStore::RemoveUser(const string& user_id) {
  std::scoped_lock lock{users_mutex};
  users_online.erase(user_id);
  return absl::OkStatus();
}

StatusOr<unordered_set<string>> InMemoryGameStore::GetUsers() const {
  std::scoped_lock lock{users_mutex};
  return users_online;
}

StatusOr<GameStatePtr> InMemoryGameStore::NewGame(const GameStatePtr game_state_no_id) {
  std::scoped_lock lock{game_state_mutex};
  string game_id = std::to_string(counter++);
  auto game_state = std::make_shared<GameState>(game_state_no_id->withIdAndVersion(game_id, "foo"));
  auto user_id_maybe = game_state->getPlayer(0).getName();
  if (user_id_maybe->empty()) {
    return absl::InternalError(
        "game_state cannot be created without a player. This should have been validated upstream.");
  }
  auto user_id = user_id_maybe.value();

  if (game_ids_by_user_id.contains(user_id)) {
    return absl::InvalidArgumentError("already in game");
  }

  auto emplaceWorked = games_by_id.emplace(game_state->getGameId(), game_state);
  if (!emplaceWorked.second) {
    return absl::InvalidArgumentError("could not generate unused game id");
  }

  game_ids_by_user_id[user_id] = game_id;
  return games_by_id.at(game_id);
}

StatusOr<GameStatePtr> InMemoryGameStore::ReadGame(const string& game_id) const {
  std::scoped_lock lock{game_state_mutex};
  if (games_by_id.contains(game_id)) {
    return games_by_id.at(game_id);
  }
  return absl::NotFoundError("game not found");
}

StatusOr<GameStatePtr> InMemoryGameStore::ReadGameByUserId(const string& user_id) const {
  std::scoped_lock lock{game_state_mutex};
  if (game_ids_by_user_id.contains(user_id)) {
    return games_by_id.at(game_ids_by_user_id.at(user_id));
  }
  return absl::NotFoundError("game not found");
}

StatusOr<GameStatePtr> InMemoryGameStore::UpdateGame(const GameStatePtr game_state) {
  std::scoped_lock lock{game_state_mutex};
  auto game_id = game_state->getGameId();
  if (!games_by_id.contains(game_id)) {
    return absl::InvalidArgumentError("game does not exist");
  }
  if (games_by_id.at(game_id)->isOver()) {
    return absl::InvalidArgumentError("game is over");
  }

  for (auto p : game_state->getPlayers()) {
    if (p.isPresent() && p.getName().has_value()) {
      game_ids_by_user_id[p.getName().value()] = game_id;
    }
  }

  games_by_id[game_id] = game_state;
  return game_state;
}

StatusOr<unordered_set<GameStatePtr>> InMemoryGameStore::ReadAllGames() const {
  std::scoped_lock lock{game_state_mutex};
  // TODO: switch to ranges once llvm publishes a release build for darwin-x86_64 for a recent
  // version auto kv = std::ranges::views::values(games_by_id); return {kv.begin(), kv.end()};

  std::unordered_set<GameStatePtr> games{};
  for (auto& [_, game] : games_by_id) {
    games.insert(game);
  }
  return games;
}
}  // namespace golf
