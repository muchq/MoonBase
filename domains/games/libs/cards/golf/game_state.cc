#include "domains/games/libs/cards/golf/game_state.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/player.h"

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

bool GameState::allPlayersPeeked() const {
  return std::all_of(players.begin(), players.end(),
                     [](const Player& p) { return p.hasCompletedPeeks(); });
}

bool GameState::revealCountdownActive() const {
  return !peeksHidden && !isOver() && allPlayersPeeked();
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

StatusOr<GameState> GameState::peekOwnCard(int player, Position position) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return FailedPreconditionError("no such player");
  }
  if (whoKnocked != -1) {
    return FailedPreconditionError("cannot peek after a knock");
  }

  auto updatedPlayer = players.at(player).addPeek(position);
  if (!updatedPlayer.ok()) {
    return updatedPlayer.status();
  }

  vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) == player) {
      updatedPlayers.push_back(*updatedPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }

  return GameState{drawPile,         discardPile, std::move(updatedPlayers),
                   peekedAtDrawPile, whoseTurn,   whoKnocked,
                   peeksHidden,      gameId,      version_id};
}

StatusOr<GameState> GameState::hideCards(int player) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return FailedPreconditionError("no such player");
  }
  if (!revealCountdownActive()) {
    return FailedPreconditionError("no reveal countdown to end");
  }

  vector<Player> updatedPlayers;
  updatedPlayers.reserve(players.size());
  for (const Player& p : players) {
    updatedPlayers.push_back(p.clearPeeks());
  }

  return GameState{drawPile,
                   discardPile,
                   std::move(updatedPlayers),
                   peekedAtDrawPile,
                   whoseTurn,
                   whoKnocked,
                   true,
                   gameId,
                   version_id};
}

StatusOr<GameState> GameState::peekAtDrawPile(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (revealCountdownActive()) {
    return FailedPreconditionError("waiting for peeked cards to be hidden");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  if (peekedAtDrawPile) {
    return FailedPreconditionError("you can only peek once per turn");
  }

  return GameState{drawPile,   discardPile, players, true,      whoseTurn,
                   whoKnocked, peeksHidden, gameId,  version_id};
}

StatusOr<GameState> GameState::swapDrawForDiscardPile(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (revealCountdownActive()) {
    return FailedPreconditionError("waiting for peeked cards to be hidden");
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
                   peeksHidden,
                   gameId,
                   version_id};
}

StatusOr<GameState> GameState::swapForDrawPile(int player, Position position) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (revealCountdownActive()) {
    return FailedPreconditionError("waiting for peeked cards to be hidden");
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
                   peeksHidden,
                   gameId,
                   version_id};
}

absl::StatusOr<GameState> GameState::swapForDiscardPile(int player, Position position) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (revealCountdownActive()) {
    return FailedPreconditionError("waiting for peeked cards to be hidden");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  if (peekedAtDrawPile) {
    return FailedPreconditionError("cannot swap for discard after peeking");
  }

  // remove top card from discard pile
  deque<Card> mutableDiscardPile{discardPile};

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

  return GameState{drawPile,
                   discardPileForNewGameState,
                   playersForNewGameState,
                   false,
                   newWhoseTurn,
                   whoKnocked,
                   peeksHidden,
                   gameId,
                   version_id};
}

StatusOr<GameState> GameState::knock(int player) const {
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (!allPlayersPresent()) {
    return FailedPreconditionError("not all players have joined");
  }
  if (revealCountdownActive()) {
    return FailedPreconditionError("waiting for peeked cards to be hidden");
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

  return GameState{drawPile, discardPile, players, false,     newWhoseTurn,
                   player,   peeksHidden, gameId,  version_id};
}

absl::StatusOr<GameState> GameState::removePlayer(int player) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return FailedPreconditionError("no such player");
  }

  vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (static_cast<int>(i) != player) {
      updatedPlayers.push_back(players.at(i));
    }
  }

  int newWhoseTurn = whoseTurn;
  if (player < newWhoseTurn) {
    newWhoseTurn--;
  }
  if (!updatedPlayers.empty()) {
    newWhoseTurn = newWhoseTurn % static_cast<int>(updatedPlayers.size());
  } else {
    newWhoseTurn = 0;
  }

  int newWhoKnocked = whoKnocked;
  if (player == newWhoKnocked) {
    newWhoKnocked = -1;  // the knocker fled; the knock is void
  } else if (newWhoKnocked != -1 && player < newWhoKnocked) {
    newWhoKnocked--;
  }

  // Below two players there is no game left to play: force isOver so the
  // remaining seat's win resolves through the ordinary scoring path.
  if (updatedPlayers.size() < 2) {
    newWhoKnocked = newWhoseTurn;
  }

  return GameState{drawPile,    discardPile,  std::move(updatedPlayers),
                   false,       newWhoseTurn, newWhoKnocked,
                   peeksHidden, gameId,       version_id};
}

GameState GameState::withPlayers(vector<Player> newPlayers) const {
  return GameState{drawPile,    discardPile, std::move(newPlayers),
                   false,       whoseTurn,   whoKnocked,
                   peeksHidden, gameId,      version_id};
}

GameState GameState::withIdAndVersion(const std::string& _game_id,
                                      const std::string& _version_id) const {
  return GameState{drawPile,   discardPile, players,  peekedAtDrawPile, whoseTurn,
                   whoKnocked, peeksHidden, _game_id, _version_id};
}

}  // namespace golf
