#include "cpp/cards/golf/game_state.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/player.h"

namespace golf {
using namespace cards;

bool GameState::isOver() const { return drawPile.empty() || whoseTurn == whoKnocked; }

bool GameState::allPlayersPresent() const {
  for (const auto& p : players) {
    if (!p.isPresent()) {
      return false;
    }
  }
  return true;
}

std::unordered_set<int> GameState::winners() const {
  std::unordered_set<int> winningPlayers;
  int minScore = 40;  // max score is 9 10 Q K == 39
  int playerIndex = 0;
  for (auto& p : players) {
    int playerScore = p.score();
    if (playerScore < minScore) {
      minScore = playerScore;
      winningPlayers.clear();
    }
    if (playerScore == minScore) {
      winningPlayers.insert(playerIndex);
    }

    playerIndex++;
  }
  if (winningPlayers.find(whoKnocked) != winningPlayers.end()) {
    winningPlayers.clear();
    winningPlayers.insert(whoKnocked);
  }

  return winningPlayers;
}

absl::StatusOr<GameState> GameState::swapForDrawPile(int player, Position position) const {
  if (isOver()) {
    return absl::FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return absl::FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return absl::FailedPreconditionError("not your turn");
  }

  // update draw pile
  std::deque<Card> updatedDrawPile{drawPile};
  Card toSwampIntoHand = updatedDrawPile.back();
  updatedDrawPile.pop_back();
  const std::deque<Card> drawPileForNewGameState = std::move(updatedDrawPile);

  // update current player
  const Player currentPlayer = players.at(player);
  Card toSwapOutOfHand = currentPlayer.cardAt(position);
  const Player updatedCurrentPlayer = currentPlayer.swapCard(toSwampIntoHand, position);

  // update players list
  std::vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) == whoseTurn) {
      updatedPlayers.push_back(updatedCurrentPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }
  const std::vector<Player> playersForNewGameState = std::move(updatedPlayers);

  // update discard pile
  std::deque<Card> updatedDiscardPile{discardPile};
  updatedDiscardPile.push_back(toSwapOutOfHand);
  const std::deque<Card> discardPileForNewGameState = std::move(updatedDiscardPile);

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{drawPileForNewGameState,
                   discardPileForNewGameState,
                   playersForNewGameState,
                   newWhoseTurn,
                   whoKnocked,
                   gameId};
}

absl::StatusOr<GameState> GameState::swapForDiscardPile(int player, Position position) const {
  if (isOver()) {
    return absl::FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return absl::FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return absl::FailedPreconditionError("not your turn");
  }

  // remove top card from discard pile
  std::deque<Card> mutableDiscardPile{discardPile};

  // TODO: how should we enforce looking at the card once?
  Card toSwampIntoHand = mutableDiscardPile.back();
  mutableDiscardPile.pop_back();

  // update current player
  const Player currentPlayer = players.at(player);
  Card toSwapOutOfHand = currentPlayer.cardAt(position);
  const Player updatedCurrentPlayer = currentPlayer.swapCard(toSwampIntoHand, position);

  // update discardPile
  mutableDiscardPile.push_back(toSwapOutOfHand);
  const std::deque<Card> discardPileForNewGameState = std::move(mutableDiscardPile);

  // update players list
  std::vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) == whoseTurn) {
      updatedPlayers.push_back(updatedCurrentPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }
  const std::vector<Player> playersForNewGameState = std::move(updatedPlayers);

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{
      drawPile, discardPileForNewGameState, playersForNewGameState, newWhoseTurn, whoKnocked,
      gameId};
}

absl::StatusOr<GameState> GameState::knock(int player) const {
  if (isOver()) {
    return absl::FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return absl::FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return absl::FailedPreconditionError("not your turn");
  }

  if (whoKnocked != -1) {
    return absl::FailedPreconditionError("someone already knocked");
  }

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{drawPile, discardPile, players, newWhoseTurn, player, gameId};
}

GameState GameState::withPlayers(std::vector<Player> newPlayers) const {
  return GameState{drawPile, discardPile, std::move(newPlayers), whoseTurn, whoKnocked, gameId};
}

}  // namespace golf
