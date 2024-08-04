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
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

auto validate_user_id(const string& user_id) -> Status {
  if (user_id.size() < 4 || user_id.size() > 15) {
    return InvalidArgumentError("username length must be between 4 and 15 chars");
  }

  if (user_id.find_first_not_of(allowedChars) != string::npos) {
    return InvalidArgumentError("only alphanumeric, underscore, or dash allowed in username");
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

// https://stackoverflow.com/a/444614/599075
std::mt19937 GameManager::randomGenerator() const {
  auto constexpr seed_bytes = sizeof(std::mt19937) * std::mt19937::state_size;
  auto constexpr seed_len = seed_bytes / sizeof(std::seed_seq::result_type);
  auto seed = std::array<std::seed_seq::result_type, seed_len>();
  auto dev = std::random_device();
  std::generate_n(begin(seed), seed_len, std::ref(dev));
  auto seed_seq = std::seed_seq(begin(seed), end(seed));
  return std::mt19937{seed_seq};
}

std::string GameManager::generateRandomAlphanumericString(std::size_t len) const {
  static constexpr auto chars =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  thread_local auto rng = randomGenerator();
  auto dist = std::uniform_int_distribution{{}, std::strlen(chars) - 1};
  auto result = std::string(len, '\0');
  std::generate_n(begin(result), len, [&]() { return chars[dist(rng)]; });
  return result;
}

std::optional<std::string> GameManager::generateUnusedRandomId() const {
  for (int i = 0; i < 10; i++) {
    auto attempt = generateRandomAlphanumericString(12);
    auto existing_game = game_store_->ReadGame(attempt);
    if (!existing_game.ok()) {
      return attempt;
    }
  }
  return {};
}

// TODO: support multiple decks for many players?
StatusOr<GameStatePtr> GameManager::newGame(const string& user_id, int number_of_players) {
  if (!game_store_->UserExists(user_id)) {
    return InvalidArgumentError("unknown user");
  }

  if (number_of_players < 2 || number_of_players > 5) {
    return InvalidArgumentError("2 to 5 players");
  }

  auto gameIdMaybe = generateUnusedRandomId();
  if (!gameIdMaybe.has_value()) {
    return InvalidArgumentError("could not generate unused game id");
  }
  const auto gameId = gameIdMaybe.value();
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
      std::make_shared<GameState>(GameState{drawPile, discardPile, players, false, 0, -1, gameId});
  return game_store_->NewGame(game_state);
}

StatusOr<GameStatePtr> GameManager::joinGame(const string& game_id, const string& user_id) {
  if (!game_store_->UserExists(user_id)) {
    return InvalidArgumentError("unregistered username");
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

StatusOr<GameStatePtr> GameManager::getGameStateForUser(const string& user_id) const {
  if (!game_store_->UserExists(user_id)) {
    return InvalidArgumentError("unknown user");
  }

  return game_store_->ReadGameByUserId(user_id);
}

StatusOr<GameStatePtr> GameManager::updateGameState(StatusOr<GameState> updateResult,
                                                    const string& gameId) {
  if (!updateResult.ok()) {
    return InvalidArgumentError(updateResult.status().message());
  }

  auto game_state = std::make_shared<GameState>(*updateResult);
  return game_store_->UpdateGame(game_state);
}

StatusOr<GameStatePtr> GameManager::peekAtDrawPile(const string& username) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->peekAtDrawPile(playerIndex), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapDrawForDiscardPile(const string& username) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->swapDrawForDiscardPile(playerIndex), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapForDrawPile(const string& username, Position position) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->swapForDrawPile(playerIndex, position), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::swapForDiscardPile(const string& username, Position position) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->swapForDiscardPile(playerIndex, position), game->getGameId());
}

StatusOr<GameStatePtr> GameManager::knock(const string& username) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

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
  return game_store_->ReadAllGames();
}

std::unordered_map<string, string> GameManager::getGameIdsByUserId() const {
  auto games_result = game_store_->ReadAllGames();
  std::unordered_map<string, string> game_ids_by_user{};
  for (auto g : games_result) {
    auto game_id = g->getGameId();
    for (auto p : g->getPlayers()) {
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
  for (auto p : (*game_maybe)->getPlayers()) {
    if (p.isPresent() && p.getName().has_value()) {
      users.insert(p.getName().value());
    }
  }
  return users;
}

}  // namespace golf
