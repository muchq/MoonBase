#ifndef CPP_CARDS_RUMMY_TABLE_H
#define CPP_CARDS_RUMMY_TABLE_H

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/game_state.h"

namespace rummy {

/// The games a rummy deal can be (#1609). Basic is GameState's rules;
/// gin (#1610) and rummy 500 (#1611) join here.
enum class Variant { Basic };

/// The wire's and the stats pipeline's word for a variant.
[[nodiscard]] std::string_view variantName(Variant variant);
[[nodiscard]] std::optional<Variant> parseVariant(std::string_view name);
/// The variants a table of this many seats may deal.
[[nodiscard]] std::vector<Variant> variantsFor(int seats);

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

class TableState {
 public:
  /// A table opening for these seats: choosing, seat 0 dealing.
  [[nodiscard]] static absl::StatusOr<TableState> open(const std::string& game_id,
                                                       const std::vector<std::string>& seats);

  TableState(std::vector<std::string> _seats, std::vector<int> _wins, int _dealer, int _dealNumber,
             TablePhase _phase, Variant _variant, std::optional<GameState> _deal,
             std::string _gameId, std::string _versionId);

  /// The next deal, from an already-shuffled deck. `seat` must be the
  /// dealer's, or any seat's when the dealer is away.
  [[nodiscard]] absl::StatusOr<TableState> chooseVariant(int seat, Variant variant,
                                                         std::deque<Card> shuffled_deck,
                                                         bool dealerAway = false) const;

  /// A move in the deal in play; the table follows it, dealing on to
  /// choosing when the deal ends.
  template <typename Move>
  [[nodiscard]] absl::StatusOr<TableState> inDeal(const Move& move) const {
    if (phase != TablePhase::Playing || !deal.has_value()) {
      return absl::FailedPreconditionError("no deal in play");
    }
    absl::StatusOr<GameState> next = move(*deal);
    if (!next.ok()) return next.status();
    return afterDeal(*std::move(next));
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
  [[nodiscard]] const std::optional<GameState>& getDeal() const { return deal; }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] const std::string& getGameId() const { return gameId; }
  [[nodiscard]] const std::string& getVersionId() const { return versionId; }
  [[nodiscard]] TableState withIdAndVersion(const std::string& game_id,
                                            const std::string& version_id) const;

 private:
  [[nodiscard]] TableState afterDeal(GameState next) const;

  std::vector<std::string> seats;
  std::vector<int> wins;
  int dealer;
  int dealNumber;
  TablePhase phase;
  Variant variant;
  std::optional<GameState> deal;
  std::string gameId;
  std::string versionId;
};

}  // namespace rummy

#endif
