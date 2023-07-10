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

static const std::string allowedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

absl::StatusOr<std::string> GameManager::registerUser(const std::string& username) {
  if (username.size() < 4 || username.size() > 15) {
    return absl::InvalidArgumentError("username length must be between 4 and 15 chars");
  }

  if (username.find_first_not_of(allowedChars) != std::string::npos) {
    return absl::InvalidArgumentError("only alphanumeric, underscore, or dash allowed in username");
  }

  if (usersOnline.find(username) != usersOnline.end()) {
    return absl::InvalidArgumentError("username taken");
  }

  usersOnline.insert(username);
  return username;
}

std::deque<Card> GameManager::shuffleNewDeck() {
  std::vector<int> cards{};
  for (int i = 0; i < 52; i++) {
    cards.push_back(i);
  }

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(cards.begin(), cards.end(), g);

  std::deque<Card> deck{};
  for (auto c : cards) {
    deck.emplace_back(c);
  }
  return deck;
}

// TODO: generate random game id
// TODO: support multiple decks
absl::StatusOr<std::shared_ptr<GameState>> GameManager::newGame(const std::string& username,
                                                                int numberOfPlayers) {
  if (usersOnline.find(username) == usersOnline.end()) {
    return absl::InvalidArgumentError("unregistered username");
  }

  if (gameIdsByUser.find(username) != gameIdsByUser.end()) {
    return absl::InvalidArgumentError("already in game");
  }

  if (numberOfPlayers < 2 || numberOfPlayers > 5) {
    return absl::InvalidArgumentError("2 to 5 players");
  }

  const std::string gameId = "foo";  // generateUnusedRandomId();
  std::deque<Card> mutableDrawPile = shuffleNewDeck();

  std::vector<Card> allDealt{};
  for (int i = 0; i < numberOfPlayers * 4; i++) {
    allDealt.push_back(mutableDrawPile.back());
    mutableDrawPile.pop_back();
  }

  std::vector<Player> mutablePlayers;

  // two up, two down
  int halfway = numberOfPlayers * 2;
  for (int i = 0; i < numberOfPlayers; i++) {
    int tl_idx = 2 * i;
    int tr_idx = 2 * i + 1;
    int bl_idx = 2 * i + halfway;
    int br_idx = 2 * i + halfway + 1;
    if (i == 0) {
      mutablePlayers.emplace_back(username, allDealt.at(tl_idx), allDealt.at(tr_idx),
                                  allDealt.at(bl_idx), allDealt.at(br_idx));
    } else {
      mutablePlayers.emplace_back(allDealt.at(tl_idx), allDealt.at(tr_idx), allDealt.at(bl_idx),
                                  allDealt.at(br_idx));
    }
  }

  const std::vector<Player> players = std::move(mutablePlayers);

  std::deque<Card> mutableDiscardPile{mutableDrawPile.back()};
  mutableDrawPile.pop_back();

  const std::deque<Card> drawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile = std::move(mutableDiscardPile);

  gamesById.emplace(gameId, std::make_shared<GameState>(
                                GameState{drawPile, discardPile, players, 0, -1, gameId}));
  usersByGame.insert(std::make_pair(gameId, std::unordered_set<std::string>{username}));
  return gamesById.at(gameId);
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::joinGame(const std::string& gameId,
                                                                 const std::string& username) {
  if (usersOnline.find(username) == usersOnline.end()) {
    return absl::InvalidArgumentError("unregistered username");
  }

  auto gameIter = gamesById.find(gameId);
  if (gameIter == gamesById.end()) {
    return absl::InvalidArgumentError("unknown game id");
  }

  auto oldGameState = gameIter->second;

  if (oldGameState->allPlayersPresent()) {
    return absl::InvalidArgumentError("no spots available");
  }

  auto& existingPlayers = oldGameState->getPlayers();
  std::vector<Player> updatedPlayers{};
  bool playerAdded = false;
  for (auto& p : existingPlayers) {
    if (p.isPresent() || playerAdded) {
      updatedPlayers.push_back(p);
    } else {
      updatedPlayers.emplace_back(
          *p.claimHand(username));  // safe because we know player is not already claimed
      playerAdded = true;
    }
  }

  gamesById.erase(gameId);
  gamesById.emplace(gameId, std::make_shared<GameState>(oldGameState->withPlayers(updatedPlayers)));
  usersByGame.at(gameId).insert(username);

  return gamesById.at(gameId);
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::getGameStateForUser(
    const std::string& username) const {
  if (usersOnline.find(username) == usersOnline.end()) {
    return absl::InvalidArgumentError("unregistered username");
  }

  if (gameIdsByUser.find(username) == gameIdsByUser.end()) {
    return absl::InvalidArgumentError("user not in game");
  }

  return gamesById.at(gameIdsByUser.at(username));
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::updateGameState(
    absl::StatusOr<GameState> updateResult, const std::string& gameId) {
  if (!updateResult.ok()) {
    return absl::InvalidArgumentError(updateResult.status().message());
  }

  gamesById.erase(gameId);
  gamesById.emplace(gameId, std::make_shared<GameState>(*updateResult));

  return gamesById.at(gameId);
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::swapForDrawPile(const std::string& username,
                                                                        Position position) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return absl::InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->swapForDrawPile(playerIndex, position), game->getGameId());
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::swapForDiscardPile(
    const std::string& username, Position position) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return absl::InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->swapForDiscardPile(playerIndex, position), game->getGameId());
}

absl::StatusOr<std::shared_ptr<GameState>> GameManager::knock(const std::string& username) {
  auto gameRes = getGameStateForUser(username);
  if (!gameRes.ok()) {
    return absl::InvalidArgumentError(gameRes.status().message());
  }

  auto game = *gameRes;
  int playerIndex = game->playerIndex(username);

  return updateGameState(game->knock(playerIndex), game->getGameId());
}

}  // namespace golf
