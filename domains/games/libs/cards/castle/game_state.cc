#include "domains/games/libs/cards/castle/game_state.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/player.h"

namespace castle {
using namespace cards;
using absl::FailedPreconditionError;
using absl::InvalidArgumentError;
using absl::StatusOr;
using std::deque;
using std::string;
using std::vector;

namespace {

bool isSpecial(Rank rank) { return rank == Rank::Two || rank == Rank::Ten; }

int rankValue(Rank rank) { return static_cast<int>(rank); }

vector<Card> take(const vector<Card>& row, const vector<int>& indexes) {
  vector<Card> taken;
  taken.reserve(indexes.size());
  for (int index : indexes) {
    taken.push_back(row.at(index));
  }
  return taken;
}

// The roster with one seat replaced; Player is not assignable.
vector<Player> replaceSeat(const vector<Player>& roster, int seat, const Player& replacement) {
  vector<Player> out;
  out.reserve(roster.size());
  for (size_t i = 0; i < roster.size(); i++) {
    out.push_back(static_cast<int>(i) == seat ? replacement : roster.at(i));
  }
  return out;
}

vector<Player> withoutSeat(const vector<Player>& roster, int seat) {
  vector<Player> out;
  out.reserve(roster.size());
  for (size_t i = 0; i < roster.size(); i++) {
    if (static_cast<int>(i) != seat) {
      out.push_back(roster.at(i));
    }
  }
  return out;
}

// The seat holding the lowest ordinary card in hand; specials do not
// count, and a table with none goes to seat 0.
int openingSeat(const vector<Player>& roster) {
  int seat = 0;
  int lowest = rankValue(Rank::Ace) + 1;
  for (size_t i = 0; i < roster.size(); i++) {
    for (const Card& card : roster.at(i).getHand()) {
      if (!isSpecial(card.getRank()) && rankValue(card.getRank()) < lowest) {
        lowest = rankValue(card.getRank());
        seat = static_cast<int>(i);
      }
    }
  }
  return seat;
}

int seatsHoldingCards(const vector<Player>& roster) {
  return static_cast<int>(
      std::count_if(roster.begin(), roster.end(), [](const Player& p) { return !p.isOut(); }));
}

}  // namespace

StatusOr<GameState> dealCastleGame(const string& game_id, const vector<string>& player_ids,
                                   deque<Card> shuffled_deck) {
  const int seats = static_cast<int>(player_ids.size());
  if (seats < GameState::kMinPlayers || seats > GameState::kMaxPlayers) {
    return InvalidArgumentError("2 to 4 players");
  }
  if (shuffled_deck.size() < player_ids.size() * 3 * GameState::kHandSize) {
    return InvalidArgumentError("deck too small");
  }
  vector<Player> players;
  players.reserve(player_ids.size());
  for (const string& player_id : player_ids) {
    vector<Card> rows[3];
    for (auto& row : rows) {
      for (int i = 0; i < GameState::kHandSize; i++) {
        row.push_back(shuffled_deck.back());
        shuffled_deck.pop_back();
      }
    }
    players.emplace_back(player_id, std::move(rows[2]), std::move(rows[1]), std::move(rows[0]));
  }
  return GameState{std::move(shuffled_deck),
                   {},
                   std::move(players),
                   GameState::kNoTurn,
                   Phase::Setup,
                   {},
                   game_id,
                   ""};
}

absl::Status GameState::ensureSeat(int player) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return InvalidArgumentError("no such player");
  }
  return absl::OkStatus();
}

absl::Status GameState::ensurePlayableTurn(int player) const {
  if (auto seat = ensureSeat(player); !seat.ok()) {
    return seat;
  }
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  if (phase == Phase::Setup) {
    return FailedPreconditionError("still setting up");
  }
  if (whoseTurn != player) {
    return FailedPreconditionError("not your turn");
  }
  return absl::OkStatus();
}

StatusOr<GameState> GameState::swapForSetup(int player, int handIndex, int faceUpIndex) const {
  if (auto seat = ensureSeat(player); !seat.ok()) {
    return seat;
  }
  if (phase != Phase::Setup) {
    return FailedPreconditionError("setup is over");
  }
  if (players.at(player).isReady()) {
    return FailedPreconditionError("already ready");
  }
  auto swapped = players.at(player).swapForSetup(handIndex, faceUpIndex);
  if (!swapped.ok()) {
    return swapped.status();
  }
  return GameState{drawPile,  pile,     replaceSeat(players, player, *swapped),
                   whoseTurn, phase,    finished,
                   gameId,    versionId};
}

StatusOr<GameState> GameState::ready(int player) const {
  if (auto seat = ensureSeat(player); !seat.ok()) {
    return seat;
  }
  if (phase != Phase::Setup) {
    return FailedPreconditionError("setup is over");
  }
  if (players.at(player).isReady()) {
    return FailedPreconditionError("already ready");
  }
  vector<Player> newPlayers = replaceSeat(players, player, players.at(player).withReady());
  const bool everyoneReady = std::all_of(newPlayers.begin(), newPlayers.end(),
                                         [](const Player& p) { return p.isReady(); });
  const int turn = everyoneReady ? openingSeat(newPlayers) : kNoTurn;
  const Phase newPhase = everyoneReady ? Phase::Playing : Phase::Setup;
  return GameState{drawPile, pile,     std::move(newPlayers), turn, newPhase, finished,
                   gameId,   versionId};
}

std::optional<Card> GameState::pileTop() const {
  if (pile.empty()) {
    return std::nullopt;
  }
  return pile.back();
}

bool GameState::isPlayable(Rank rank) const {
  if (isSpecial(rank) || pile.empty()) {
    return true;
  }
  // A two on top takes anything, which the rank order already says: two
  // is the lowest rank.
  return rankValue(rank) >= rankValue(pile.back().getRank());
}

bool GameState::hasLegalPlay(int player) const {
  if (!ensureSeat(player).ok()) {
    return false;
  }
  const Player& seat = players.at(player);
  if (seat.source() == Source::FaceDown) {
    return false;
  }
  const vector<Card>& row = seat.row(seat.source());
  return std::any_of(row.begin(), row.end(),
                     [this](const Card& card) { return isPlayable(card.getRank()); });
}

StatusOr<GameState> GameState::playFromHand(int player, const vector<int>& indexes) const {
  return play(player, Source::Hand, indexes);
}

StatusOr<GameState> GameState::playFaceUp(int player, const vector<int>& indexes) const {
  return play(player, Source::FaceUp, indexes);
}

StatusOr<GameState> GameState::play(int player, Source source, const vector<int>& indexes) const {
  if (auto turn = ensurePlayableTurn(player); !turn.ok()) {
    return turn;
  }
  const Player& seat = players.at(player);
  if (seat.source() != source) {
    return FailedPreconditionError("not the row in play");
  }
  if (indexes.empty()) {
    return InvalidArgumentError("no cards");
  }
  auto remaining = seat.without(source, indexes);
  if (!remaining.ok()) {
    return remaining.status();
  }
  const vector<Card> played = take(seat.row(source), indexes);
  const Rank rank = played.front().getRank();
  if (std::any_of(played.begin(), played.end(),
                  [rank](const Card& card) { return card.getRank() != rank; })) {
    return InvalidArgumentError("one rank per play");
  }
  if (!isPlayable(rank)) {
    return FailedPreconditionError("that rank cannot go on the pile");
  }

  deque<Card> newDrawPile = drawPile;
  vector<Card> drawn;
  if (source == Source::Hand) {
    while (static_cast<int>(remaining->getHand().size() + drawn.size()) < kHandSize &&
           !newDrawPile.empty()) {
      drawn.push_back(newDrawPile.back());
      newDrawPile.pop_back();
    }
  }
  vector<Player> newPlayers = replaceSeat(players, player, remaining->withHandAdded(drawn));

  vector<Card> newPile = pile;
  for (const Card& card : played) {
    newPile.push_back(card);
  }
  int run = 0;
  for (auto it = newPile.rbegin(); it != newPile.rend() && it->getRank() == rank; ++it) {
    run++;
  }
  const bool burned = rank == Rank::Ten || run >= 4;
  return settle(player, std::move(newDrawPile), std::move(newPile), std::move(newPlayers), burned);
}

StatusOr<GameState> GameState::playFaceDown(int player, int index) const {
  if (auto turn = ensurePlayableTurn(player); !turn.ok()) {
    return turn;
  }
  const Player& seat = players.at(player);
  if (seat.source() != Source::FaceDown) {
    return FailedPreconditionError("not the row in play");
  }
  auto remaining = seat.without(Source::FaceDown, {index});
  if (!remaining.ok()) {
    return remaining.status();
  }
  const Card card = seat.getFaceDown().at(index);
  if (isPlayable(card.getRank())) {
    return play(player, Source::FaceDown, {index});
  }
  // Unplayable: the seat takes the pile and the card it turned over.
  vector<Card> taken = pile;
  taken.push_back(card);
  vector<Player> newPlayers = replaceSeat(players, player, remaining->withHandAdded(taken));
  const int next = nextSeat(player, newPlayers);
  return GameState{drawPile, {}, std::move(newPlayers), next, phase, finished, gameId, versionId};
}

StatusOr<GameState> GameState::pickUp(int player) const {
  if (auto turn = ensurePlayableTurn(player); !turn.ok()) {
    return turn;
  }
  if (pile.empty()) {
    return FailedPreconditionError("nothing to pick up");
  }
  if (players.at(player).source() == Source::FaceDown) {
    return FailedPreconditionError("face-down cards are played blind");
  }
  if (hasLegalPlay(player)) {
    return FailedPreconditionError("a playable card must be played");
  }
  vector<Player> newPlayers = replaceSeat(players, player, players.at(player).withHandAdded(pile));
  const int next = nextSeat(player, newPlayers);
  return GameState{drawPile, {}, std::move(newPlayers), next, phase, finished, gameId, versionId};
}

GameState GameState::settle(int player, deque<Card> newDrawPile, vector<Card> newPile,
                            vector<Player> newPlayers, bool burned) const {
  if (burned) {
    newPile.clear();
  }
  vector<string> newFinished = finished;
  const bool wentOut = newPlayers.at(player).isOut();
  if (wentOut) {
    newFinished.push_back(newPlayers.at(player).getId());
  }
  if (seatsHoldingCards(newPlayers) <= 1) {
    return GameState{std::move(newDrawPile),
                     std::move(newPile),
                     std::move(newPlayers),
                     kNoTurn,
                     Phase::Over,
                     std::move(newFinished),
                     gameId,
                     versionId};
  }
  const int next = burned && !wentOut ? player : nextSeat(player, newPlayers);
  return GameState{std::move(newDrawPile),
                   std::move(newPile),
                   std::move(newPlayers),
                   next,
                   phase,
                   std::move(newFinished),
                   gameId,
                   versionId};
}

// The next seat after `from` that still holds cards.
int GameState::nextSeat(int from, const vector<Player>& roster) const {
  const int seats = static_cast<int>(roster.size());
  for (int step = 1; step <= seats; step++) {
    const int candidate = (from + step) % seats;
    if (!roster.at(candidate).isOut()) {
      return candidate;
    }
  }
  return kNoTurn;
}

StatusOr<GameState> GameState::removePlayer(int player) const {
  if (auto seat = ensureSeat(player); !seat.ok()) {
    return seat;
  }
  if (isOver()) {
    return FailedPreconditionError("game is over");
  }
  vector<Player> newPlayers = withoutSeat(players, player);
  if (newPlayers.size() < static_cast<size_t>(kMinPlayers)) {
    return GameState{drawPile, pile,     std::move(newPlayers), kNoTurn, Phase::Abandoned, finished,
                     gameId,   versionId};
  }
  int newTurn = whoseTurn;
  if (phase == Phase::Playing) {
    if (whoseTurn == player) {
      // The turn passes as if the leaver had just played, on the roster
      // that still has them so the search starts from their seat.
      newTurn = nextSeat(player, players);
    }
    if (newTurn > player) {
      newTurn--;
    }
  }
  Phase newPhase = phase;
  if (phase == Phase::Setup && std::all_of(newPlayers.begin(), newPlayers.end(),
                                           [](const Player& p) { return p.isReady(); })) {
    newTurn = openingSeat(newPlayers);
    newPhase = Phase::Playing;
  }
  if (phase == Phase::Playing && seatsHoldingCards(newPlayers) <= 1) {
    return GameState{drawPile, pile,     std::move(newPlayers), kNoTurn, Phase::Over, finished,
                     gameId,   versionId};
  }
  return GameState{drawPile, pile,     std::move(newPlayers), newTurn, newPhase, finished,
                   gameId,   versionId};
}

std::optional<string> GameState::loser() const {
  if (phase != Phase::Over) {
    return std::nullopt;
  }
  for (const Player& p : players) {
    if (!p.isOut()) {
      return p.getId();
    }
  }
  return std::nullopt;
}

GameState GameState::withIdAndVersion(const string& game_id, const string& version_id) const {
  return GameState{drawPile, pile, players, whoseTurn, phase, finished, game_id, version_id};
}

int GameState::playerIndex(const string& id) const {
  for (size_t i = 0; i < players.size(); i++) {
    if (players.at(i).getId() == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace castle
