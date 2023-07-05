#include "cpp/cards/golf/golf.h"

#include <unordered_set>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

const int Player::score() const {
  std::unordered_set<Rank> hand;
  int score = 0;
  for (auto c : allCards()) {
    if (hand.find(c.getRank()) != hand.end()) {  // pairs cancel each other
      score -= cardValue(c);
      hand.erase(c.getRank());
    } else {
      score += cardValue(c);
      hand.insert(c.getRank());
    }
  }
  return score;
}

const bool Player::isPresent() const {
  return name.has_value();
}

const absl::StatusOr<Player> Player::claimHand(std::string username) const {
  if (isPresent()) {
    return absl::FailedPreconditionError("already claimed");
  }
  return Player{username, topLeft, topRight, bottomLeft, bottomRight};
}

const std::vector<Card> Player::allCards() const {
  std::vector<Card> all;
  all.push_back(topLeft);
  all.push_back(topRight);
  all.push_back(bottomLeft);
  all.push_back(bottomRight);
  return all;
}

const int Player::cardValue(Card c) const {
  switch (c.getRank()) {
    case cards::Rank::Ace:
      return 1;
    case cards::Rank::Two:
      return 2;
    case cards::Rank::Three:
      return 3;
    case cards::Rank::Four:
      return 4;
    case cards::Rank::Five:
      return 5;
    case cards::Rank::Six:
      return 6;
    case cards::Rank::Seven:
      return 7;
    case cards::Rank::Eight:
      return 8;
    case cards::Rank::Nine:
      return 9;
    case cards::Rank::Ten:
      return 10;
    case cards::Rank::Jack:
      return 0;
    case cards::Rank::Queen:
      return 10;
    case cards::Rank::King:
      return 10;
    default:
      return -1;  // error
  }
}

const Card Player::cardAt(Position position) const {
  if (position == Position::TopLeft) {
    return topLeft;
  } else if (position == Position::TopRight) {
    return topRight;
  } else if (position == Position::BottomLeft) {
    return bottomLeft;
  } else {
    return bottomRight;
  }
}

const Player Player::swapCard(Card toSwap, Position position) const {
  if (position == Position::TopLeft) {
    return Player{name, toSwap, topRight, bottomLeft, bottomRight};
  } else if (position == Position::TopRight) {
    return Player{name, topLeft, toSwap, bottomLeft, bottomRight};
  } else if (position == Position::BottomLeft) {
    return Player{name, topLeft, topRight, toSwap, bottomRight};
  } else {
    return Player{name, topLeft, topRight, bottomLeft, toSwap};
  }
}

const bool GameState::isOver() const { return drawPile.empty() || whoseTurn == whoKnocked; }

const bool GameState::allPlayersPresent() const {
  for (auto p : players) {
    if (!p.isPresent()) {
      return false;
    }
  }
  return true;
}

const std::unordered_set<int> GameState::winners() const {
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

const absl::StatusOr<GameState> GameState::swapForDrawPile(int player, Position position) const {
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
    if (i == whoseTurn) {
      updatedPlayers.push_back(updatedCurrentPlayer);
    } else {
      updatedPlayers.push_back(players.at(i));
    }
  }
  const std::vector<Player> playersForNewGameState = std::move(updatedPlayers);

  // update discard pile
  std::deque<Card> updatedDiscardPile{discardPile};
  updatedDiscardPile.push_back(toSwapOutOfHand.flipped());
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

const absl::StatusOr<GameState> GameState::swapForDiscardPile(int player, Position position) const {
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
  Card toSwampIntoHand = mutableDiscardPile.back().flipped();
  mutableDiscardPile.pop_back();

  // update current player
  const Player currentPlayer = players.at(player);
  Card toSwapOutOfHand = currentPlayer.cardAt(position);
  const Player updatedCurrentPlayer = currentPlayer.swapCard(toSwampIntoHand, position);

  // update discardPile
  mutableDiscardPile.push_back(toSwapOutOfHand.flipped());
  const std::deque<Card> discardPileForNewGameState = std::move(mutableDiscardPile);

  // update players list
  std::vector<Player> updatedPlayers;
  for (size_t i = 0; i < players.size(); i++) {
    if (i == whoseTurn) {
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

const absl::StatusOr<GameState> GameState::knock(int player) const {
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

static const std::string allowedChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

const absl::StatusOr<std::string> GameManager::registerUser(std::string username) {
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

const absl::StatusOr<GameRef> GameManager::newGame(std::string username, int numberOfPlayers) {
  if (usersOnline.find(username) == usersOnline.end()) {
    return absl::InvalidArgumentError("unregistered username");
  }

  if (gameIdsByUserId.find(username) != gameIdsByUserId.end()) {
    return absl::InvalidArgumentError("already in game");
  }

  if (numberOfPlayers < 2 || numberOfPlayers > 5) {
    return absl::InvalidArgumentError("2 to 5 players");
  }

  const std::string gameId = "foo"; // generateUnusedRandomId();
  std::deque<Card> mutableDrawPile{};// = shuffleNewDeck();

  std::vector<Card> allDealt{};
  for (int i = 0; i < numberOfPlayers * 4; i++) {
    allDealt.push_back(mutableDrawPile.back());
    mutableDrawPile.pop_back();
  }

  std::vector<Player> mutablePlayers;

  // two up, two down
  int halfway = numberOfPlayers * 2;
  for (int i = 0; i < numberOfPlayers; i++) {
    int tl_idx = 2*i;
    int tr_idx = 2*i + 1;
    int bl_idx = 2*i + halfway;
    int br_idx = 2*i + halfway + 1;
    if (i == 0) {
      mutablePlayers.push_back(Player{username, allDealt.at(tl_idx), allDealt.at(tr_idx), allDealt.at(bl_idx), allDealt.at(br_idx)});
    } else {
      mutablePlayers.push_back(Player{allDealt.at(tl_idx), allDealt.at(tr_idx), allDealt.at(bl_idx), allDealt.at(br_idx)});
    }
  }

  const std::vector<Player> players = std::move(mutablePlayers);

  std::deque<Card> mutableDiscardPile{mutableDrawPile.back().flipped()};
  mutableDrawPile.pop_back();

  const std::deque<Card> drawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile = std::move(mutableDiscardPile);

  gamesById.emplace(std::make_pair(gameId, GameState{drawPile, discardPile, players, 0, -1, gameId}));
  return gamesById.at(gameId);
}