#ifndef CPP_CARDS_RUMMY_TABLE_H
#define CPP_CARDS_RUMMY_TABLE_H

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/gin.h"

namespace rummy {

/// The games a rummy deal can be (#1609). Seven-card is GameState's rules,
/// seven cards a seat; ten-card is the same game dealt ten; gin is
/// GinState's (#1610). Rummy 500 (#1611) joins here.
enum class Variant { SevenCard, TenCard, Gin };

/// The wire's word for a variant.
[[nodiscard]] std::string_view variantName(Variant variant);
/// The word a deal of this variant is recorded under as a game (#1571):
/// GameState's rummy, at either hand size, keeps "rummy", the word the game had
/// before it had variants.
[[nodiscard]] std::string_view recordedName(Variant variant);
[[nodiscard]] std::optional<Variant> parseVariant(std::string_view name);
/// The variants a table of this many seats may deal.
[[nodiscard]] std::vector<Variant> variantsFor(int seats);

/// A deal of any variant: seven- and ten-card play GameState, gin
/// GinState.
using Deal = std::variant<GameState, GinState>;

/// What any deal says of itself, whatever its game.
[[nodiscard]] const std::vector<Player>& dealPlayers(const Deal& deal);
[[nodiscard]] Phase dealPhase(const Deal& deal);
[[nodiscard]] std::optional<std::string> dealWinner(const Deal& deal);
[[nodiscard]] int dealWinnerPoints(const Deal& deal);
[[nodiscard]] int dealDeadwood(const Deal& deal, int seat);
/// The seat on turn; kNoTurn once the deal is over.
[[nodiscard]] int dealWhoseTurn(const Deal& deal);

/// A rummy table (#1609): dealer's choice, one deal after another until
/// the table breaks up.
///
///   - A table opens choosing, its first seat the dealer. Between deals
///     the dealer picks the next deal's variant from those that fit the
///     seats; a dealer who is away — the hub says so — lets any seat pick.
///   - The seat after the dealer opens the deal. When the deal ends the
///     dealer moves one seat on and the table is choosing again, the
///     finished deal kept to show its hands and result.
///   - A deal won by play is a hand won in the standings. A table below
///     two seats is closed, and so is a deal in progress with it.
///
/// Seats are the deal's seats while one is in play; a leaver goes from
/// both, and the standings compact with them.
enum class TablePhase { Choosing, Playing, Closed };

/// A line of the table's score sheet: one deal's end — its game, the seat
/// that won it (none for a gin draw) and what it scored. Names, not seats:
/// a line outlasts its seat leaving.
struct DealScore {
  Variant variant = Variant::SevenCard;
  std::optional<std::string> winner;
  int points = 0;
  bool operator==(const DealScore& o) const {
    return variant == o.variant && winner == o.winner && points == o.points;
  }
};

class TableState {
 public:
  /// A table opening for these seats: choosing, seat 0 dealing.
  [[nodiscard]] static absl::StatusOr<TableState> open(const std::string& game_id,
                                                       const std::vector<std::string>& seats);

  TableState(std::vector<std::string> _seats, std::vector<int> _wins, int _dealer, int _dealNumber,
             TablePhase _phase, Variant _variant, std::optional<Deal> _deal, std::string _gameId,
             std::string _versionId, std::vector<DealScore> _scoreSheet = {});

  /// The next deal, from an already-shuffled deck. `seat` must be the
  /// dealer's, or any seat's when the dealer is away.
  [[nodiscard]] absl::StatusOr<TableState> chooseVariant(int seat, Variant variant,
                                                         std::deque<Card> shuffled_deck,
                                                         bool dealerAway = false) const;

  /// A move in the deal in play, of `Engine`'s game; the table follows
  /// it, dealing on to choosing when the deal ends. A move of another
  /// game than the deal's is refused.
  template <typename Engine, typename Move>
  [[nodiscard]] absl::StatusOr<TableState> inDeal(const Move& move) const {
    if (phase != TablePhase::Playing || !deal.has_value()) {
      return absl::FailedPreconditionError("no deal in play");
    }
    const Engine* engine = std::get_if<Engine>(&*deal);
    if (engine == nullptr) {
      return absl::FailedPreconditionError(absl::StrCat("not a move in ", variantName(variant)));
    }
    absl::StatusOr<Engine> next = move(*engine);
    if (!next.ok()) return next.status();
    return afterDeal(Deal(*std::move(next)));
  }

  /// A seat leaving, from the table and any deal in play.
  [[nodiscard]] absl::StatusOr<TableState> removePlayer(int seat) const;

  [[nodiscard]] bool isOver() const { return phase == TablePhase::Closed; }
  [[nodiscard]] TablePhase getPhase() const { return phase; }
  [[nodiscard]] const std::vector<std::string>& getSeats() const { return seats; }
  /// Hands won, seat by seat.
  [[nodiscard]] const std::vector<int>& getWins() const { return wins; }
  [[nodiscard]] int getDealer() const { return dealer; }
  /// Deals dealt so far; zero before the first.
  [[nodiscard]] int getDealNumber() const { return dealNumber; }
  /// The variant of the deal in play, or of the last one.
  [[nodiscard]] Variant getVariant() const { return variant; }
  /// The deal in play, or the one that just ended; absent before the first.
  [[nodiscard]] const std::optional<Deal>& getDeal() const { return deal; }
  /// The deal as GameState or GinState, if it is that engine's.
  [[nodiscard]] const GameState* rummyDeal() const {
    return deal.has_value() ? std::get_if<GameState>(&*deal) : nullptr;
  }
  [[nodiscard]] const GinState* ginDeal() const {
    return deal.has_value() ? std::get_if<GinState>(&*deal) : nullptr;
  }
  /// Every deal played to its end, in order.
  [[nodiscard]] const std::vector<DealScore>& getScoreSheet() const { return scoreSheet; }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] const std::string& getGameId() const { return gameId; }
  [[nodiscard]] const std::string& getVersionId() const { return versionId; }
  [[nodiscard]] TableState withIdAndVersion(const std::string& game_id,
                                            const std::string& version_id) const;

 private:
  [[nodiscard]] TableState afterDeal(Deal next) const;

  std::vector<std::string> seats;
  std::vector<int> wins;
  int dealer;
  int dealNumber;
  TablePhase phase;
  Variant variant;
  std::optional<Deal> deal;
  std::string gameId;
  std::string versionId;
  std::vector<DealScore> scoreSheet;
};

}  // namespace rummy

#endif
