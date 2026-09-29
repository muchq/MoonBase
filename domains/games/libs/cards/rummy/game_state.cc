#include "domains/games/libs/cards/rummy/game_state.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/meld.h"

namespace rummy {
using absl::FailedPreconditionError;
using absl::InvalidArgumentError;
using absl::StatusOr;
using std::deque;
using std::vector;

namespace {

// The hand without these cards, or NotFound naming the first it lacks.
StatusOr<vector<Card>> without(const vector<Card>& hand, const vector<Card>& cards) {
  vector<Card> rest = hand;
  for (const Card& card : cards) {
    const auto at = std::find(rest.begin(), rest.end(), card);
    if (at == rest.end()) return absl::NotFoundError("that card is not in your hand");
    rest.erase(at);
  }
  return rest;
}

vector<Player> withHand(const vector<Player>& roster, int seat, vector<Card> hand) {
  vector<Player> out = roster;
  out.at(seat).hand = std::move(hand);
  return out;
}

}  // namespace

StatusOr<GameState> dealRummyGame(const string& game_id, const vector<string>& player_ids,
                                  deque<Card> shuffled_deck, int opener, int hand_size) {
  const int seats = static_cast<int>(player_ids.size());
  if (seats < GameState::kMinPlayers || seats > GameState::kMaxPlayers) {
    return InvalidArgumentError("2 to 4 players");
  }
  if (opener < 0 || opener >= seats) return InvalidArgumentError("no such opener");
  if (hand_size < 1) return InvalidArgumentError("no cards to deal");
  if (static_cast<int>(shuffled_deck.size()) < seats * hand_size + 1) {
    return InvalidArgumentError("deck too small");
  }
  vector<Player> players;
  players.reserve(player_ids.size());
  for (const string& id : player_ids) players.push_back(Player{id, {}});
  for (int round = 0; round < hand_size; round++) {
    for (Player& player : players) {
      player.hand.push_back(shuffled_deck.back());
      shuffled_deck.pop_back();
    }
  }
  vector<Card> discard{shuffled_deck.back()};
  shuffled_deck.pop_back();
  return GameState{std::move(shuffled_deck),
                   std::move(discard),
                   std::move(players),
                   {},
                   opener,
                   Stage::Draw,
                   Phase::Playing,
                   std::nullopt,
                   game_id,
                   ""};
}

absl::Status GameState::ensureTurn(int player, Stage wanted) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return InvalidArgumentError("no such player");
  }
  if (isOver()) return FailedPreconditionError("game is over");
  if (whoseTurn != player) return FailedPreconditionError("not your turn");
  if (stage != wanted) {
    return FailedPreconditionError(wanted == Stage::Draw ? "you have already drawn"
                                                         : "draw a card first");
  }
  return absl::OkStatus();
}

bool GameState::canDrawStock() const { return !stock.empty() || discardPile.size() > 1; }

StatusOr<GameState> GameState::drawStock(int player) const {
  if (auto turn = ensureTurn(player, Stage::Draw); !turn.ok()) return turn;
  if (!canDrawStock()) return FailedPreconditionError("the stock is empty");
  deque<Card> newStock = stock;
  vector<Card> newDiscard = discardPile;
  if (newStock.empty()) {
    // Turned over, all but the top: the pile's bottom card is the stock's
    // new top.
    newStock.assign(newDiscard.rbegin() + 1, newDiscard.rend());
    newDiscard.erase(newDiscard.begin(), newDiscard.end() - 1);
  }
  vector<Card> hand = players.at(player).hand;
  hand.push_back(newStock.back());
  newStock.pop_back();
  return GameState{std::move(newStock),
                   std::move(newDiscard),
                   withHand(players, player, std::move(hand)),
                   melds,
                   player,
                   Stage::Play,
                   phase,
                   std::nullopt,
                   gameId,
                   versionId,
                   LastMove{players.at(player).id, MoveKind::DrawStock, {}, -1}};
}

vector<Card> GameState::discardTakeable(int player) const {
  if (!ensureTurn(player, Stage::Draw).ok() || discardPile.empty()) return {};
  vector<vector<Card>> table;
  table.reserve(melds.size());
  for (const Meld& meld : melds) table.push_back(meld.cards);
  vector<Card> takeable;
  for (size_t at = 0; at + 1 < discardPile.size(); at++) {
    vector<Card> pool = players.at(player).hand;
    pool.insert(pool.end(), discardPile.begin() + static_cast<std::ptrdiff_t>(at),
                discardPile.end());
    if (playable(discardPile.at(at), pool, table)) takeable.push_back(discardPile.at(at));
  }
  takeable.push_back(discardPile.back());
  return takeable;
}

StatusOr<GameState> GameState::drawDiscard(int player, std::optional<Card> downTo) const {
  if (auto turn = ensureTurn(player, Stage::Draw); !turn.ok()) return turn;
  if (discardPile.empty()) return FailedPreconditionError("the discard pile is empty");
  if (downTo.has_value() && *downTo != discardPile.back()) {
    const auto at = std::find(discardPile.begin(), discardPile.end(), *downTo);
    if (at == discardPile.end())
      return InvalidArgumentError("that card is not in the discard pile");
    const vector<Card> takeable = discardTakeable(player);
    if (std::find(takeable.begin(), takeable.end(), *downTo) == takeable.end()) {
      return FailedPreconditionError("you could not play the " + faceOf(*downTo));
    }
    vector<Card> taken(at, discardPile.end());
    vector<Card> hand = players.at(player).hand;
    hand.insert(hand.end(), taken.begin(), taken.end());
    return GameState{stock,
                     vector<Card>(discardPile.begin(), at),
                     withHand(players, player, std::move(hand)),
                     melds,
                     player,
                     Stage::Play,
                     phase,
                     std::nullopt,
                     gameId,
                     versionId,
                     LastMove{players.at(player).id, MoveKind::DrawDiscard, std::move(taken), -1},
                     *downTo};
  }
  const Card top = discardPile.back();
  vector<Card> newDiscard(discardPile.begin(), discardPile.end() - 1);
  vector<Card> hand = players.at(player).hand;
  hand.push_back(top);
  return GameState{stock,
                   std::move(newDiscard),
                   withHand(players, player, std::move(hand)),
                   melds,
                   player,
                   Stage::Play,
                   phase,
                   top,
                   gameId,
                   versionId,
                   LastMove{players.at(player).id, MoveKind::DrawDiscard, {top}, -1}};
}

StatusOr<GameState> GameState::meld(int player, const vector<Card>& cards) const {
  if (auto turn = ensureTurn(player, Stage::Play); !turn.ok()) return turn;
  auto rest = without(players.at(player).hand, cards);
  if (!rest.ok()) return rest.status();
  auto arranged = arrangedMeld(cards);
  if (!arranged.has_value()) return InvalidArgumentError("those cards are not a set or a run");
  vector<Meld> table = melds;
  table.push_back(Meld{players.at(player).id, *arranged});
  const int index = static_cast<int>(table.size()) - 1;
  return afterLaying(player, *std::move(rest), std::move(table),
                     LastMove{players.at(player).id, MoveKind::Meld, *arranged, index});
}

StatusOr<GameState> GameState::layOff(int player, const Card& card, int meldIndex) const {
  if (auto turn = ensureTurn(player, Stage::Play); !turn.ok()) return turn;
  if (meldIndex < 0 || meldIndex >= static_cast<int>(melds.size())) {
    return InvalidArgumentError("no such meld");
  }
  auto rest = without(players.at(player).hand, {card});
  if (!rest.ok()) return rest.status();
  vector<Card> grown = melds.at(meldIndex).cards;
  grown.push_back(card);
  auto arranged = arrangedMeld(grown);
  if (!arranged.has_value()) return InvalidArgumentError("that card does not fit that meld");
  vector<Meld> table = melds;
  table.at(meldIndex).cards = *std::move(arranged);
  return afterLaying(player, *std::move(rest), std::move(table),
                     LastMove{players.at(player).id, MoveKind::LayOff, {card}, meldIndex});
}

StatusOr<GameState> GameState::discard(int player, const Card& card) const {
  if (auto turn = ensureTurn(player, Stage::Play); !turn.ok()) return turn;
  auto rest = without(players.at(player).hand, {card});
  if (!rest.ok()) return rest.status();
  if (takenDiscard == card && !rest->empty()) {
    return FailedPreconditionError("you took that card from the discard pile this turn");
  }
  if (mustPlay.has_value()) {
    return FailedPreconditionError("play the " + faceOf(*mustPlay) + " you took first");
  }
  vector<Card> newDiscard = discardPile;
  newDiscard.push_back(card);
  const bool out = rest->empty();
  const int seats = static_cast<int>(players.size());
  return GameState{stock,
                   std::move(newDiscard),
                   withHand(players, player, *std::move(rest)),
                   melds,
                   out ? kNoTurn : (player + 1) % seats,
                   Stage::Draw,
                   out ? Phase::Over : phase,
                   std::nullopt,
                   gameId,
                   versionId,
                   LastMove{players.at(player).id, MoveKind::Discard, {card}, -1}};
}

StatusOr<GameState> GameState::afterLaying(int player, vector<Card> hand, vector<Meld> table,
                                           LastMove move) const {
  const bool out = hand.empty();
  // The card owed stays owed until it leaves the hand, and must stay
  // playable while it is there.
  std::optional<Card> owed = mustPlay;
  if (owed.has_value() && std::find(hand.begin(), hand.end(), *owed) == hand.end()) owed.reset();
  if (owed.has_value()) {
    vector<vector<Card>> laid;
    for (const Meld& meld : table) laid.push_back(meld.cards);
    if (!playable(*owed, hand, laid)) {
      return FailedPreconditionError("that would leave the " + faceOf(*owed) +
                                     " you took unplayable");
    }
  }
  return GameState{stock,
                   discardPile,
                   withHand(players, player, std::move(hand)),
                   std::move(table),
                   out ? kNoTurn : player,
                   Stage::Play,
                   out ? Phase::Over : phase,
                   takenDiscard,
                   gameId,
                   versionId,
                   std::move(move),
                   owed};
}

StatusOr<GameState> GameState::removePlayer(int player) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return InvalidArgumentError("no such player");
  }
  if (isOver()) return FailedPreconditionError("game is over");
  vector<Player> rest = players;
  rest.erase(rest.begin() + player);
  if (rest.size() < static_cast<size_t>(kMinPlayers)) {
    return GameState{stock,   discardPile, std::move(rest),  melds,
                     kNoTurn, stage,       Phase::Abandoned, takenDiscard,
                     gameId,  versionId,   lastMove,         std::nullopt};
  }
  int turn = whoseTurn;
  Stage newStage = stage;
  std::optional<Card> taken = takenDiscard;
  std::optional<Card> owed = mustPlay;
  if (whoseTurn == player) {
    // The seat after the leaver's takes its place, from the draw; past the
    // end of the table that is the first seat.
    turn = player % static_cast<int>(rest.size());
    newStage = Stage::Draw;
    taken.reset();
    owed.reset();
  } else if (whoseTurn > player) {
    turn--;
  }
  return GameState{stock, discardPile, std::move(rest), melds,     turn,     newStage,
                   phase, taken,       gameId,          versionId, lastMove, owed};
}

std::optional<string> GameState::winner() const {
  if (phase != Phase::Over) return std::nullopt;
  for (const Player& p : players) {
    if (p.hand.empty()) return p.id;
  }
  return std::nullopt;
}

int GameState::deadwood(int player) const {
  int points = 0;
  for (const Card& card : players.at(player).hand) points += cardPoints(card);
  return points;
}

int GameState::winnerPoints() const {
  if (!winner().has_value()) return 0;
  int points = 0;
  for (size_t i = 0; i < players.size(); i++) points += deadwood(static_cast<int>(i));
  return points;
}

GameState GameState::withIdAndVersion(const string& game_id, const string& version_id) const {
  return GameState{stock, discardPile,  players, melds,      whoseTurn, stage,
                   phase, takenDiscard, game_id, version_id, lastMove,  mustPlay};
}

int GameState::playerIndex(const string& id) const {
  for (size_t i = 0; i < players.size(); i++) {
    if (players.at(i).id == id) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace rummy
