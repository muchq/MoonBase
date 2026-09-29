#include "domains/games/libs/cards/rummy/gin.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/rummy/arrange.h"

namespace rummy {
using absl::FailedPreconditionError;
using absl::InvalidArgumentError;
using absl::NotFoundError;
using absl::StatusOr;

StatusOr<GinState> dealGin(const std::string& game_id, const std::vector<std::string>& player_ids,
                           std::deque<Card> shuffled_deck, int opener) {
  if (static_cast<int>(player_ids.size()) != GinState::kSeats) {
    return InvalidArgumentError("gin is for 2 players");
  }
  if (opener < 0 || opener >= GinState::kSeats) return InvalidArgumentError("no such opener");
  if (static_cast<int>(shuffled_deck.size()) < GinState::kSeats * GinState::kHandSize + 1) {
    return InvalidArgumentError("deck too small");
  }
  std::vector<Player> players;
  for (const std::string& id : player_ids) players.push_back(Player{id, {}});
  for (int round = 0; round < GinState::kHandSize; round++) {
    for (Player& player : players) {
      player.hand.push_back(shuffled_deck.back());
      shuffled_deck.pop_back();
    }
  }
  std::vector<Card> discard{shuffled_deck.back()};
  shuffled_deck.pop_back();
  return GinState{std::move(shuffled_deck),
                  std::move(discard),
                  std::move(players),
                  opener,
                  GinStage::Upcard,
                  Phase::Playing,
                  0,
                  std::nullopt,
                  std::nullopt,
                  std::nullopt,
                  game_id,
                  ""};
}

GinState::GinState(std::deque<Card> _stock, std::vector<Card> _discard,
                   std::vector<Player> _players, int _whoseTurn, GinStage _stage, Phase _phase,
                   int _upcardPasses, std::optional<Card> _takenDiscard,
                   std::optional<GinLastMove> _lastMove, std::optional<GinResult> _result,
                   std::string _gameId, std::string _versionId)
    : stock(std::move(_stock)),
      discardPile(std::move(_discard)),
      players(std::move(_players)),
      whoseTurn(_whoseTurn),
      stage(_stage),
      phase(_phase),
      upcardPasses(_upcardPasses),
      takenDiscard(std::move(_takenDiscard)),
      lastMove(std::move(_lastMove)),
      result(std::move(_result)),
      gameId(std::move(_gameId)),
      versionId(std::move(_versionId)) {}

absl::Status GinState::ensureTurn(int player) const {
  if (phase != Phase::Playing) return FailedPreconditionError("game is over");
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return InvalidArgumentError("no such player");
  }
  if (player != whoseTurn) return FailedPreconditionError("not your turn");
  return absl::OkStatus();
}

StatusOr<GinState> GinState::pass(int player) const {
  if (auto status = ensureTurn(player); !status.ok()) return status;
  if (stage != GinStage::Upcard) return FailedPreconditionError("nothing to pass on");
  const int other = 1 - player;
  // The first pass offers the upcard to the dealer; the second leaves the
  // opener — the seat that passed first — to draw from the stock.
  const bool second = upcardPasses > 0;
  GinState next = *this;
  next.whoseTurn = other;
  next.stage = second ? GinStage::StockOnly : GinStage::Upcard;
  next.upcardPasses = second ? 0 : 1;
  next.lastMove = GinLastMove{players[player].id, GinMoveKind::Pass, {}};
  return next;
}

StatusOr<GinState> GinState::drawStock(int player) const {
  if (auto status = ensureTurn(player); !status.ok()) return status;
  if (stage == GinStage::Upcard) return FailedPreconditionError("take the upcard or pass");
  if (stage == GinStage::Play) return FailedPreconditionError("you have already drawn");
  if (stock.empty()) return FailedPreconditionError("the stock is empty");
  GinState next = *this;
  next.players[player].hand.push_back(next.stock.back());
  next.stock.pop_back();
  next.stage = GinStage::Play;
  next.takenDiscard = std::nullopt;
  next.lastMove = GinLastMove{players[player].id, GinMoveKind::DrawStock, {}};
  return next;
}

StatusOr<GinState> GinState::drawDiscard(int player) const {
  if (auto status = ensureTurn(player); !status.ok()) return status;
  if (stage == GinStage::Play) return FailedPreconditionError("you have already drawn");
  if (stage == GinStage::StockOnly) {
    return FailedPreconditionError("the upcard was passed: draw from the stock");
  }
  if (discardPile.empty()) return FailedPreconditionError("the discard pile is empty");
  GinState next = *this;
  const Card taken = next.discardPile.back();
  next.discardPile.pop_back();
  next.players[player].hand.push_back(taken);
  next.stage = GinStage::Play;
  next.upcardPasses = 0;
  next.takenDiscard = taken;
  next.lastMove = GinLastMove{players[player].id, GinMoveKind::DrawDiscard, {taken}};
  return next;
}

StatusOr<std::vector<Card>> GinState::throwing(int player, const Card& card) const {
  if (auto status = ensureTurn(player); !status.ok()) return status;
  if (stage != GinStage::Play) return FailedPreconditionError("draw a card first");
  std::vector<Card> hand = players[player].hand;
  const auto at = std::find(hand.begin(), hand.end(), card);
  if (at == hand.end()) return NotFoundError("that card is not in your hand");
  if (takenDiscard == card) {
    return FailedPreconditionError("you took that card from the discard pile this turn");
  }
  hand.erase(at);
  return hand;
}

StatusOr<GinState> GinState::discard(int player, const Card& card) const {
  auto hand = throwing(player, card);
  if (!hand.ok()) return hand.status();
  GinState next = *this;
  next.players[player].hand = *std::move(hand);
  next.discardPile.push_back(card);
  next.takenDiscard = std::nullopt;
  next.lastMove = GinLastMove{players[player].id, GinMoveKind::Discard, {card}};
  if (static_cast<int>(next.stock.size()) <= kStockFloor) {
    // Down to the last two: the deal is drawn, and nobody scores.
    GinResult drawn;
    drawn.ending = GinEnding::Draw;
    for (const Player& seat : next.players) drawn.hands.push_back(bestArrangement(seat.hand));
    next.result = std::move(drawn);
    next.phase = Phase::Over;
    next.whoseTurn = kNoTurn;
    return next;
  }
  next.whoseTurn = 1 - player;
  next.stage = GinStage::Draw;
  return next;
}

StatusOr<GinState> GinState::knock(int player, const Card& card) const {
  auto hand = throwing(player, card);
  if (!hand.ok()) return hand.status();
  const Arrangement knocker = bestArrangement(*hand);
  if (knocker.deadwoodPoints > kKnockLimit) {
    return FailedPreconditionError("more than 10 deadwood: no knock");
  }
  const int defender = 1 - player;
  GinResult ended;
  ended.knocker = player;
  ended.hands.resize(kSeats);
  ended.hands[player] = knocker;
  if (knocker.deadwoodPoints == 0) {
    // Gin: nothing to lay off onto.
    ended.ending = GinEnding::Gin;
    ended.hands[defender] = bestArrangement(players[defender].hand);
    ended.winner = player;
    ended.points = kGinBonus + ended.hands[defender].deadwoodPoints;
  } else {
    LaidOffArrangement defended = bestWithLayOffs(players[defender].hand, knocker.melds);
    ended.hands[defender] = std::move(defended.own);
    ended.laidOff = std::move(defended.laidOff);
    const int kept = ended.hands[defender].deadwoodPoints;
    if (kept <= knocker.deadwoodPoints) {
      ended.ending = GinEnding::Undercut;
      ended.winner = defender;
      ended.points = kUndercutBonus + knocker.deadwoodPoints - kept;
    } else {
      ended.ending = GinEnding::Knock;
      ended.winner = player;
      ended.points = kept - knocker.deadwoodPoints;
    }
  }
  GinState next = *this;
  next.players[player].hand = *std::move(hand);
  next.discardPile.push_back(card);
  next.takenDiscard = std::nullopt;
  next.lastMove = GinLastMove{players[player].id, GinMoveKind::Knock, {card}};
  next.result = std::move(ended);
  next.phase = Phase::Over;
  next.whoseTurn = kNoTurn;
  return next;
}

StatusOr<GinState> GinState::removePlayer(int player) const {
  if (player < 0 || player >= static_cast<int>(players.size())) {
    return InvalidArgumentError("no such player");
  }
  if (phase != Phase::Playing) return FailedPreconditionError("game is over");
  GinState next = *this;
  next.players.erase(next.players.begin() + player);
  next.phase = Phase::Abandoned;
  next.whoseTurn = kNoTurn;
  next.takenDiscard = std::nullopt;
  return next;
}

std::optional<std::string> GinState::winner() const {
  if (phase != Phase::Over || !result.has_value() || result->winner < 0) return std::nullopt;
  return players.at(result->winner).id;
}

int GinState::deadwood(int player) const {
  if (result.has_value() && player < static_cast<int>(result->hands.size())) {
    return result->hands[player].deadwoodPoints;
  }
  return bestArrangement(players.at(player).hand).deadwoodPoints;
}

bool GinState::canDrawStock() const {
  return phase == Phase::Playing && (stage == GinStage::StockOnly || stage == GinStage::Draw) &&
         !stock.empty();
}

bool GinState::canDrawDiscard() const {
  return phase == Phase::Playing && (stage == GinStage::Upcard || stage == GinStage::Draw) &&
         !discardPile.empty();
}

GinState GinState::withIdAndVersion(const std::string& game_id,
                                    const std::string& version_id) const {
  GinState next = *this;
  next.gameId = game_id;
  next.versionId = version_id;
  return next;
}

int GinState::playerIndex(const std::string& id) const {
  for (std::size_t i = 0; i < players.size(); i++) {
    if (players[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace rummy
