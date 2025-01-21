#include "cpp/cards/golf/game_manager.h"

#include <algorithm>
#include <random>
#include <unordered_set>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/game_state.h"
#include "cpp/cards/golf/player.h"

namespace golf {
using namespace cards;

using absl::InvalidArgumentError;
using absl::Status;
using absl::StatusOr;

using std::deque;
using std::string;
using std::vector;

static const std::string allowedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-@.";

auto validate_user_id(const string& user_id) -> Status {
  if (user_id.size() < 4 || user_id.size() > 40) {
    return InvalidArgumentError("username length must be between 4 and 40 chars");
  }

  if (user_id.find_first_not_of(allowedChars) != string::npos) {
    return InvalidArgumentError(
        "only alphanumeric, underscore, @, dot, or dash allowed in username");
  }
  return absl::OkStatus();
}

StatusOr<string> GameManager::registerUser(const string& user_id) {
  auto validate_status = validate_user_id(user_id);
  if (!validate_status.ok()) {
    return validate_status;
  }

  auto save_status = game_store_->AddUser(user_id);
  if (save_status.ok()) {
    return user_id;
  }
  return save_status;
}

deque<Card> GameManager::shuffleNewDeck() {
  vector<int> cards{};
  cards.reserve(52);
  for (int i = 0; i < 52; i++) {
    cards.push_back(i);
  }

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(cards.begin(), cards.end(), g);

  deque<Card> deck{};
  for (auto c : cards) {
    deck.emplace_back(c);
  }
  return deck;
}

// TODO: support multiple decks for many players?
StatusOr<GameStatePtr> GameManager::newGame(const string& user_id, int number_of_players) {
  auto user_exists_status = game_store_->UserExists(user_id);
  if (!user_exists_status.ok()) {
    return user_exists_status.status();
  }
  if (!*user_exists_status) {
    return InvalidArgumentError("unknown user");
  }

  if (number_of_players < 2 || number_of_players > 5) {
    return InvalidArgumentError("2 to 5 players");
  }

  deque<Card> mutableDrawPile = shuffleNewDeck();

  vector<Card> allDealt{};
  for (int i = 0; i < number_of_players * 4; i++) {
    allDealt.push_back(mutableDrawPile.back());
    mutableDrawPile.pop_back();
  }

  vector<Player> mutablePlayers;

  // two up, two down
  int halfway = number_of_players * 2;
  for (int i = 0; i < number_of_players; i++) {
    auto& tl = allDealt.at(2 * i);
    auto& tr = allDealt.at(2 * i + 1);
    auto& bl = allDealt.at(2 * i + halfway);
    auto& br = allDealt.at(2 * i + halfway + 1);
    if (i == 0) {
      mutablePlayers.emplace_back(user_id, tl, tr, bl, br);
    } else {
      mutablePlayers.emplace_back(tl, tr, bl, br);
    }
  }

  const vector<Player> players = std::move(mutablePlayers);

  deque<Card> mutableDiscardPile{mutableDrawPile.back()};
  mutableDrawPile.pop_back();

  const deque<Card> drawPile = std::move(mutableDrawPile);
  const deque<Card> discardPile = std::move(mutableDiscardPile);

  auto game_state =
      std::make_shared<GameState>(GameState{drawPile, discardPile, players, false, 0, -1});
  return game_store_->NewGame(game_state);
}

StatusOr<GameStatePtr> GameManager::joinGame(const string& game_id, const string& user_id) {
  auto user_exists_status = game_store_->UserExists(user_id);
  if (!user_exists_status.ok()) {
    return absl::InternalError("internal error");
  }
  if (!*user_exists_status) {
    return InvalidArgumentError("unknown user");
  }

  auto game_read_status = game_store_->ReadGame(game_id);
  if (!game_read_status.ok()) {
    return InvalidArgumentError("unknown game id");
  }

  auto oldGameState = *game_read_status;

  if (oldGameState->allPlayersPresent()) {
    return InvalidArgumentError("no spots available");
  }

  auto& existingPlayers = oldGameState->getPlayers();
  vector<Player> updatedPlayers{};
  bool playerAdded = false;
  for (auto& p : existingPlayers) {
    if (p.isPresent() || playerAdded) {
      updatedPlayers.push_back(p);
    } else {
      // safe because we know player is not already claimed
      updatedPlayers.emplace_back(*p.claimHand(user_id));
      playerAdded = true;
    }
  }

  auto updated_game = std::make_shared<GameState>(oldGameState->withPlayers(updatedPlayers));
  return game_store_->UpdateGame(updated_game);
}

StatusOr<GameStatePtr> GameManager::getGameStateForUser(const string& game_id,
                                                        const string& user_id) const {
  auto user_exists_status = game_store_->UserExists(user_id);
  if (!user_exists_status.ok()) {
    return absl::InternalError("internal error");
  }
  if (!*user_exists_status) {
    return InvalidArgumentError("unknown user");
  }

  auto status_or_game = game_store_->ReadGame(game_id);
  if (!status_or_game.ok()) {
    return status_or_game.status();
  }
  auto game_read = status_or_game.value();
  if (game_read->playerIndex(user_id) < 0) {
    return InvalidArgumentError("unknown user");
  }
  return game_read;
}

StatusOr<GameStatePtr> GameManager::updateGameState(StatusOr<GameState> updateResult,
                                                    const string& gameId) {
  if (!updateResult.ok()) {
    return InvalidArgumentError(updateResult.status().message());
  }

  auto game_state = std::make_shared<GameState>(*updateResult);
  return game_store_->UpdateGame(game_state);
}

StatusOr<GameStatePtr> GameManager::peekAtDrawPile(const string& game_id, const string& user_id) {
  auto game_res = getGameStateForUser(game_id, user_id);
  if (!game_res.ok()) {
    return InvalidArgumentError(game_res.status().message());
  }

  auto game = game_res.value();
  int player_index = game->playerIndex(user_id);

  return updateGameState(game->peekAtDrawPile(player_index), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapDrawForDiscardPile(const string& game_id,
                                                           const string& user_id) {
  auto game_res = getGameStateForUser(game_id, user_id);
  if (!game_res.ok()) {
    return InvalidArgumentError(game_res.status().message());
  }

  auto game = game_res.value();
  int player_index = game->playerIndex(user_id);

  return updateGameState(game->swapDrawForDiscardPile(player_index), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapForDrawPile(const string& game_id, const string& user_id,
                                                    Position position) {
  auto game_res = getGameStateForUser(game_id, user_id);
  if (!game_res.ok()) {
    return InvalidArgumentError(game_res.status().message());
  }

  auto game = game_res.value();
  int player_index = game->playerIndex(user_id);

  return updateGameState(game->swapForDrawPile(player_index, position), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapForDiscardPile(const string& game_id, const string& user_id,
                                                       Position position) {
  auto gameRes = getGameStateForUser(game_id, user_id);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(user_id);

  return updateGameState(game->swapForDiscardPile(playerIndex, position), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::knock(const string& game_id, const string& user_id) {
  auto gameRes = getGameStateForUser(game_id, user_id);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(user_id);

  return updateGameState(game->knock(playerIndex), game->getGameId());
}

std::unordered_set<string> GameManager::getUsersOnline() const {
  auto read_users_status = game_store_->GetUsers();
  if (!read_users_status.ok()) {
    return {};  // TODO: bubble status to caller
  }
  return *read_users_status;
}

std::unordered_set<GameStatePtr> GameManager::getGames() const {
  auto all_games_status = game_store_->ReadAllGames();
  if (!all_games_status.ok()) {
    return {};
  }
  return *all_games_status;
}

std::unordered_map<string, string> GameManager::getGameIdsByUserId() const {
  auto games_result = this->getGames();
  std::unordered_map<string, string> game_ids_by_user{};
  for (auto& g : games_result) {
    auto game_id = g->getGameId();
    for (auto& p : g->getPlayers()) {
      if (p.isPresent() && p.getName().has_value()) {
        game_ids_by_user[p.getName().value()] = game_id;
      }
    }
  }
  return game_ids_by_user;
}

std::unordered_set<string> GameManager::getUsersByGameId(const string& game_id) const {
  auto game_maybe = game_store_->ReadGame(game_id);
  if (!game_maybe.ok()) {
    return {};
  }
  std::unordered_set<string> users{};
  for (auto& p : (*game_maybe)->getPlayers()) {
    if (p.isPresent() && p.getName().has_value()) {
      users.insert(p.getName().value());
    }
  }
  return users;
}

}  // namespace golf
