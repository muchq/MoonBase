#include "cpp/cards/golf/game_state.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "cpp/cards/golf/player.h"

namespace golf {
using namespace cards;
using absl::FailedPreconditionError;
using absl::StatusOr;

using std::deque;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

bool GameState::isOver() const { return drawPile.empty() || whoseTurn == whoKnocked; }

bool GameState::allPlayersPresent() const {
  return std::all_of(players.begin(), players.end(), [](const Player& p) { return p.isPresent(); });
}

unordered_set<int> GameState::winners() const {
  unordered_set<int> winningPlayers;
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

StatusOr<GameState> GameState::peekAtDrawPile(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  if (peekedAtDrawPile) {
    return FailedPreconditionError("you can only peek once per turn");
  }

  return GameState{drawPile, discardPile, players, true, whoseTurn, whoKnocked, gameId};
}

StatusOr<GameState> GameState::swapDrawForDiscardPile(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }

  // update draw pile
  deque<Card> updatedDrawPile{drawPile};
  Card toSwampIntoDiscard = updatedDrawPile.back();
  updatedDrawPile.pop_back();
  const deque<Card> drawPileForNewGameState = std::move(updatedDrawPile);

  deque<Card> updatedDiscardPile{discardPile};
  updatedDiscardPile.push_back(toSwampIntoDiscard);
  const deque<Card> discardPileForNewGameState = std::move(updatedDiscardPile);

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{drawPileForNewGameState,
                   discardPileForNewGameState,
                   players,
                   false,
                   newWhoseTurn,
                   whoKnocked,
                   gameId};
}

StatusOr<GameState> GameState::swapForDrawPile(int player, Position position) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }

  // update draw pile
  deque<Card> updatedDrawPile{drawPile};
  Card toSwampIntoHand = updatedDrawPile.back();
  updatedDrawPile.pop_back();
  const deque<Card> drawPileForNewGameState = std::move(updatedDrawPile);

  // update current player
  const Player currentPlayer = players.at(player);
  Card toSwapOutOfHand = currentPlayer.cardAt(position);
  const Player updatedCurrentPlayer = currentPlayer.swapCard(toSwampIntoHand, position);

  // update players list
  vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) == whoseTurn) {
      updatedPlayers.push_back(updatedCurrentPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }
  const vector<Player> playersForNewGameState = std::move(updatedPlayers);

  // update discard pile
  deque<Card> updatedDiscardPile{discardPile};
  updatedDiscardPile.push_back(toSwapOutOfHand);
  const deque<Card> discardPileForNewGameState = std::move(updatedDiscardPile);

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{drawPileForNewGameState,
                   discardPileForNewGameState,
                   playersForNewGameState,
                   false,
                   newWhoseTurn,
                   whoKnocked,
                   gameId};
}

absl::StatusOr<GameState> GameState::swapForDiscardPile(int player, Position position) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  if (peekedAtDrawPile) {
    return FailedPreconditionError("cannot swap for discard after peeking");
  }

  // remove top card from discard pile
  deque<Card> mutableDiscardPile{discardPile};

  // TODO: how should we enforce looking at the card once?
  Card toSwampIntoHand = mutableDiscardPile.back();
  mutableDiscardPile.pop_back();

  // update current player
  const Player currentPlayer = players.at(player);
  Card toSwapOutOfHand = currentPlayer.cardAt(position);
  const Player updatedCurrentPlayer = currentPlayer.swapCard(toSwampIntoHand, position);

  // update discardPile
  mutableDiscardPile.push_back(toSwapOutOfHand);
  const deque<Card> discardPileForNewGameState = std::move(mutableDiscardPile);

  // update players list
  vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) == whoseTurn) {
      updatedPlayers.push_back(updatedCurrentPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }
  const vector<Player> playersForNewGameState = std::move(updatedPlayers);

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{
      drawPile, discardPileForNewGameState, playersForNewGameState, false, newWhoseTurn, whoKnocked,
      gameId};
}

StatusOr<GameState> GameState::knock(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  if (peekedAtDrawPile) {
    return FailedPreconditionError("cannot knock after peeking");
  }
  if (whoKnocked != -1) {
    return FailedPreconditionError("someone already knocked");
  }

  // update whose turn it is
  int newWhoseTurn = (whoseTurn + 1) % players.size();

  return GameState{drawPile, discardPile, players, false, newWhoseTurn, player, gameId};
}

GameState GameState::withPlayers(vector<Player> newPlayers) const {
  return GameState{drawPile,   discardPile, std::move(newPlayers), false, whoseTurn,
                   whoKnocked, gameId};
}

}  // namespace golf
