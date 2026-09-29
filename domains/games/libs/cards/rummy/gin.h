#ifndef CPP_CARDS_RUMMY_GIN_H
#define CPP_CARDS_RUMMY_GIN_H

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/arrange.h"
#include "domains/games/libs/cards/rummy/game_state.h"

namespace rummy {

/// Gin rummy (#1610), one deal of it at a dealer's-choice table.
///
/// The rules this engine plays:
///   - Two seats, ten cards each, one card turned up to start the
///     discard pile; the rest is the stock.
///   - The upcard: the opener (the seat after the dealer) may take it or
///     pass; then the dealer may. If both pass, the opener draws from the
///     stock and play goes on from there.
///   - A turn is a draw (the stock or the discard pile's top) then a
///     discard. The card taken from the discard pile may not go straight
///     back. Nothing is melded during play.
///   - Knocking ends the deal: the knocker discards a card and must be
///     left with 10 or less deadwood in their best arrangement
///     (bestArrangement). The defender's hand is arranged too, laying cards
///     off onto the knocker's melds (bestWithLayOffs) — not after gin.
///   - Melds are rummy's (arrangedMeld): the ace runs low under the two
///     or high over the king, never around the corner.
///   - Scoring: a knock scores the knocker the deadwood difference; gin
///     (no deadwood) scores 25 plus the defender's deadwood; a defender
///     with deadwood no more than the knocker's undercuts, scoring 25 plus
///     the difference.
///   - A discard that leaves two cards in the stock ends the deal drawn:
///     nobody scores.
///
/// Refusals are absl statuses as GameState's: FailedPrecondition for the
/// wrong turn or the wrong part of it, NotFound for a card the hand does
/// not hold.
enum class GinStage { Upcard, StockOnly, Draw, Play };

enum class GinMoveKind { Pass, DrawStock, DrawDiscard, Discard, Knock };

/// The deal's most recent move: the card taken for a discard draw, the
/// card thrown for a discard or a knock, nothing for a pass or a stock
/// draw.
struct GinLastMove {
  std::string playerId;
  GinMoveKind kind = GinMoveKind::Pass;
  std::vector<Card> cards;
  bool operator==(const GinLastMove& o) const {
    return playerId == o.playerId && kind == o.kind && cards == o.cards;
  }
};

enum class GinEnding { Knock, Gin, Undercut, Draw };

/// How a deal ended by play: the knocker (none for a draw), each seat's
/// hand arranged (the defender's without what it laid off), the
/// defender's cards laid off onto the knocker's melds, and who scored
/// what.
struct GinResult {
  GinEnding ending = GinEnding::Draw;
  int knocker = -1;
  std::vector<Arrangement> hands;
  std::vector<Card> laidOff;
  int winner = -1;
  int points = 0;
};

class GinState;

/// Deals gin from an already-shuffled deck (drawn from the back): ten a
/// seat, one turned up. `opener` decides the upcard first.
[[nodiscard]] absl::StatusOr<GinState> dealGin(const std::string& game_id,
                                               const std::vector<std::string>& player_ids,
                                               std::deque<Card> shuffled_deck, int opener = 0);

class GinState {
 public:
  static constexpr int kSeats = 2;
  static constexpr int kHandSize = 10;
  static constexpr int kKnockLimit = 10;
  static constexpr int kGinBonus = 25;
  static constexpr int kUndercutBonus = 25;
  /// A discard leaving this few in the stock draws the deal.
  static constexpr int kStockFloor = 2;
  static constexpr int kNoTurn = -1;

  GinState(std::deque<Card> _stock, std::vector<Card> _discard, std::vector<Player> _players,
           int _whoseTurn, GinStage _stage, Phase _phase, int _upcardPasses,
           std::optional<Card> _takenDiscard, std::optional<GinLastMove> _lastMove,
           std::optional<GinResult> _result, std::string _gameId, std::string _versionId);

  [[nodiscard]] absl::StatusOr<GinState> pass(int player) const;
  [[nodiscard]] absl::StatusOr<GinState> drawStock(int player) const;
  [[nodiscard]] absl::StatusOr<GinState> drawDiscard(int player) const;
  [[nodiscard]] absl::StatusOr<GinState> discard(int player, const Card& card) const;
  [[nodiscard]] absl::StatusOr<GinState> knock(int player, const Card& card) const;
  /// A seat leaving: the deal is abandoned and nobody scores.
  [[nodiscard]] absl::StatusOr<GinState> removePlayer(int player) const;

  [[nodiscard]] bool isOver() const { return phase != Phase::Playing; }
  [[nodiscard]] Phase getPhase() const { return phase; }
  [[nodiscard]] GinStage getStage() const { return stage; }
  /// The seat that scored, once the deal ended by play; none for a draw.
  [[nodiscard]] std::optional<std::string> winner() const;
  [[nodiscard]] int winnerPoints() const { return result.has_value() ? result->points : 0; }
  /// A seat's deadwood: as the deal's end arranged it, else its hand's
  /// best arrangement.
  [[nodiscard]] int deadwood(int player) const;
  [[nodiscard]] bool canDrawStock() const;
  [[nodiscard]] bool canDrawDiscard() const;
  [[nodiscard]] const std::optional<GinResult>& getResult() const { return result; }

  [[nodiscard]] GinState withIdAndVersion(const std::string& game_id,
                                          const std::string& version_id) const;
  [[nodiscard]] const std::deque<Card>& getStock() const { return stock; }
  [[nodiscard]] const std::vector<Card>& getDiscard() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const Player& getPlayer(int index) const { return players.at(index); }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  /// Passes on the upcard so far: 0 or 1 while it is on offer.
  [[nodiscard]] int getUpcardPasses() const { return upcardPasses; }
  [[nodiscard]] const std::optional<Card>& getTakenDiscard() const { return takenDiscard; }
  [[nodiscard]] const std::optional<GinLastMove>& getLastMove() const { return lastMove; }
  [[nodiscard]] const std::string& getGameId() const { return gameId; }
  [[nodiscard]] const std::string& getVersionId() const { return versionId; }

 private:
  [[nodiscard]] absl::Status ensureTurn(int player) const;
  /// The hand after throwing `card`, refused if the hand lacks it or it
  /// is the card just taken.
  [[nodiscard]] absl::StatusOr<std::vector<Card>> throwing(int player, const Card& card) const;

  std::deque<Card> stock;         // back is the top
  std::vector<Card> discardPile;  // back is the top
  std::vector<Player> players;
  int whoseTurn;
  GinStage stage;
  Phase phase;
  int upcardPasses;
  std::optional<Card> takenDiscard;
  std::optional<GinLastMove> lastMove;
  std::optional<GinResult> result;
  std::string gameId;
  std::string versionId;
};

}  // namespace rummy

#endif
