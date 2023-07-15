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
using absl::StatusOr;

using std::deque;
using std::string;
using std::vector;

static const std::string allowedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

StatusOr<string> GameManager::registerUser(const string& username) {
  if (username.size() < 4 || username.size() > 15) {
    return InvalidArgumentError("username length must be between 4 and 15 chars");
  }

  if (username.find_first_not_of(allowedChars) != string::npos) {
    return InvalidArgumentError("only alphanumeric, underscore, or dash allowed in username");
  }

  if (usersOnline.find(username) != usersOnline.end()) {
    return InvalidArgumentError("username taken");
  }

  usersOnline.insert(username);
  return username;
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

// TODO: generate random game id
// TODO: support multiple decks
StatusOr<GameStatePtr> GameManager::newGame(const string& username, int numberOfPlayers) {
  if (usersOnline.find(username) == usersOnline.end()) {
    return InvalidArgumentError("unregistered username");
  }
  if (gameIdsByUser.find(username) != gameIdsByUser.end()) {
    return InvalidArgumentError("already in game");
  }
  if (numberOfPlayers < 2 || numberOfPlayers > 5) {
    return InvalidArgumentError("2 to 5 players");
  }

  const string gameId = "foo";  // generateUnusedRandomId();
  deque<Card> mutableDrawPile = shuffleNewDeck();

  vector<Card> allDealt{};
  for (int i = 0; i < numberOfPlayers * 4; i++) {
    allDealt.push_back(mutableDrawPile.back());
    mutableDrawPile.pop_back();
  }

  vector<Player> mutablePlayers;

  // two up, two down
  int halfway = numberOfPlayers * 2;
  for (int i = 0; i < numberOfPlayers; i++) {
    auto& tl = allDealt.at(2 * i);
    auto& tr = allDealt.at(2 * i + 1);
    auto& bl = allDealt.at(2 * i + halfway);
    auto& br = allDealt.at(2 * i + halfway + 1);
    if (i == 0) {
      mutablePlayers.emplace_back(username, tl, tr, bl, br);
    } else {
      mutablePlayers.emplace_back(tl, tr, bl, br);
    }
  }

  const vector<Player> players = std::move(mutablePlayers);

  deque<Card> mutableDiscardPile{mutableDrawPile.back()};
  mutableDrawPile.pop_back();

  const deque<Card> drawPile = std::move(mutableDrawPile);
  const deque<Card> discardPile = std::move(mutableDiscardPile);

  gamesById.emplace(gameId, std::make_shared<GameState>(
                                GameState{drawPile, discardPile, players, false, 0, -1, gameId}));
  usersByGame.insert(std::make_pair(gameId, std::unordered_set<string>{username}));
  gameIdsByUser[username] = gameId;
  return gamesById.at(gameId);
}

StatusOr<GameStatePtr> GameManager::joinGame(const string& gameId, const string& username) {
  if (usersOnline.find(username) == usersOnline.end()) {
    return InvalidArgumentError("unregistered username");
  }

  auto gameIter = gamesById.find(gameId);
  if (gameIter == gamesById.end()) {
    return InvalidArgumentError("unknown game id");
  }

  auto oldGameState = gameIter->second;

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
      updatedPlayers.emplace_back(*p.claimHand(username));
      playerAdded = true;
    }
  }

  gamesById.erase(gameId);
  gamesById.emplace(gameId, std::make_shared<GameState>(oldGameState->withPlayers(updatedPlayers)));
  usersByGame.at(gameId).insert(username);
  gameIdsByUser[username] = gameId;

  return gamesById.at(gameId);
}

StatusOr<GameStatePtr> GameManager::getGameStateForUser(const string& username) const {
  if (usersOnline.find(username) == usersOnline.end()) {
    return InvalidArgumentError("unregistered username");
  }
  if (gameIdsByUser.find(username) == gameIdsByUser.end()) {
    return InvalidArgumentError("user not in game");
  }

  return gamesById.at(gameIdsByUser.at(username));
}

StatusOr<GameStatePtr> GameManager::updateGameState(StatusOr<GameState> updateResult,
                                                    const string& gameId) {
  if (!updateResult.ok()) {
    return InvalidArgumentError(updateResult.status().message());
  }

  gamesById.erase(gameId);
  gamesById.emplace(gameId, std::make_shared<GameState>(*updateResult));

  return gamesById.at(gameId);
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

}  // namespace golf
