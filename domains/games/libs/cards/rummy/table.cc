#include "domains/games/libs/cards/rummy/table.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/game_state.h"

namespace rummy {
using absl::FailedPreconditionError;
using absl::InvalidArgumentError;
using absl::StatusOr;

std::string_view variantName(Variant variant) {
  switch (variant) {
    case Variant::Basic:
      return "basic";
    case Variant::TenCard:
      return "ten-card";
    case Variant::Gin:
      return "gin";
  }
  return "basic";
}

std::string_view recordedName(Variant variant) {
  switch (variant) {
    case Variant::Basic:
    case Variant::TenCard:
      return "rummy";
    case Variant::Gin:
      return "gin";
  }
  return "rummy";
}

std::optional<Variant> parseVariant(std::string_view name) {
  for (const Variant variant : {Variant::Basic, Variant::TenCard, Variant::Gin}) {
    if (name == variantName(variant)) return variant;
  }
  return std::nullopt;
}

const std::vector<Player>& dealPlayers(const Deal& deal) {
  return std::visit([](const auto& d) -> const std::vector<Player>& { return d.getPlayers(); },
                    deal);
}

Phase dealPhase(const Deal& deal) {
  return std::visit([](const auto& d) { return d.getPhase(); }, deal);
}

std::optional<std::string> dealWinner(const Deal& deal) {
  return std::visit([](const auto& d) { return d.winner(); }, deal);
}

int dealWinnerPoints(const Deal& deal) {
  return std::visit([](const auto& d) { return d.winnerPoints(); }, deal);
}

int dealDeadwood(const Deal& deal, int seat) {
  return std::visit([seat](const auto& d) { return d.deadwood(seat); }, deal);
}

int dealWhoseTurn(const Deal& deal) {
  return std::visit([](const auto& d) { return d.getWhoseTurn(); }, deal);
}

std::vector<Variant> variantsFor(int seats) {
  switch (seats) {
    case 2:
      return {Variant::Basic, Variant::TenCard, Variant::Gin};
    case 3:
      return {Variant::Basic, Variant::TenCard};
    case 4:
      return {Variant::Basic};
  }
  return {};
}

TableState::TableState(std::vector<std::string> _seats, std::vector<int> _wins, int _dealer,
                       int _dealNumber, TablePhase _phase, Variant _variant,
                       std::optional<Deal> _deal, std::string _gameId, std::string _versionId)
    : seats(std::move(_seats)),
      wins(std::move(_wins)),
      dealer(_dealer),
      dealNumber(_dealNumber),
      phase(_phase),
      variant(_variant),
      deal(std::move(_deal)),
      gameId(std::move(_gameId)),
      versionId(std::move(_versionId)) {}

StatusOr<TableState> TableState::open(const std::string& game_id,
                                      const std::vector<std::string>& seats) {
  const int count = static_cast<int>(seats.size());
  if (count < GameState::kMinPlayers || count > GameState::kMaxPlayers) {
    return InvalidArgumentError("2 to 4 players");
  }
  return TableState{seats,
                    std::vector<int>(seats.size(), 0),
                    0,
                    0,
                    TablePhase::Choosing,
                    Variant::Basic,
                    std::nullopt,
                    game_id,
                    ""};
}

StatusOr<TableState> TableState::chooseVariant(int seat, Variant chosen,
                                               std::deque<Card> shuffled_deck,
                                               bool dealerAway) const {
  if (seat < 0 || seat >= static_cast<int>(seats.size())) {
    return InvalidArgumentError("no such player");
  }
  if (phase != TablePhase::Choosing) return FailedPreconditionError("not between deals");
  if (seat != dealer && !dealerAway) return FailedPreconditionError("the dealer chooses");
  const std::vector<Variant> offered = variantsFor(static_cast<int>(seats.size()));
  if (std::find(offered.begin(), offered.end(), chosen) == offered.end()) {
    return InvalidArgumentError("that game does not fit this table");
  }
  const int opener = (dealer + 1) % static_cast<int>(seats.size());
  std::optional<Deal> dealt;
  if (chosen == Variant::Gin) {
    auto gin = dealGin(gameId, seats, std::move(shuffled_deck), opener);
    if (!gin.ok()) return gin.status();
    dealt.emplace(gin->withIdAndVersion(gameId, versionId));
  } else {
    auto basic = dealRummyGame(gameId, seats, std::move(shuffled_deck), opener,
                               chosen == Variant::TenCard ? 10 : GameState::kHandSize);
    if (!basic.ok()) return basic.status();
    dealt.emplace(basic->withIdAndVersion(gameId, versionId));
  }
  return TableState{
      seats,  wins,     dealer, dealNumber + 1, TablePhase::Playing, chosen, std::move(dealt),
      gameId, versionId};
}

TableState TableState::afterDeal(Deal next) const {
  if (dealPhase(next) == Phase::Playing) {
    return TableState{seats,           wins,   dealer,   dealNumber, phase, variant,
                      std::move(next), gameId, versionId};
  }
  // Over by play: the hand goes to its winner and the deal passes on.
  std::vector<int> newWins = wins;
  if (const auto winner = dealWinner(next); winner.has_value()) {
    const int at = playerIndex(*winner);
    if (at >= 0) newWins.at(at)++;
  }
  const int nextDealer = (dealer + 1) % static_cast<int>(seats.size());
  return TableState{seats,   std::move(newWins), nextDealer, dealNumber, TablePhase::Choosing,
                    variant, std::move(next),    gameId,     versionId};
}

StatusOr<TableState> TableState::removePlayer(int seat) const {
  if (seat < 0 || seat >= static_cast<int>(seats.size())) {
    return InvalidArgumentError("no such player");
  }
  if (isOver()) return FailedPreconditionError("table is closed");
  std::optional<Deal> newDeal = deal;
  if (phase == TablePhase::Playing && deal.has_value()) {
    absl::StatusOr<Deal> left = std::visit(
        [seat](const auto& d) -> absl::StatusOr<Deal> {
          auto next = d.removePlayer(seat);
          if (!next.ok()) return next.status();
          return Deal(*std::move(next));
        },
        *deal);
    if (!left.ok()) return left.status();
    newDeal.emplace(*std::move(left));
  }
  std::vector<std::string> newSeats = seats;
  newSeats.erase(newSeats.begin() + seat);
  std::vector<int> newWins = wins;
  newWins.erase(newWins.begin() + seat);
  if (newSeats.size() < static_cast<size_t>(GameState::kMinPlayers)) {
    return TableState{std::move(newSeats),
                      std::move(newWins),
                      0,
                      dealNumber,
                      TablePhase::Closed,
                      variant,
                      std::move(newDeal),
                      gameId,
                      versionId};
  }
  // The dealer's chair follows its holder. A dealer who left passes the
  // next deal to the seat after them, wrapping: between deals that seat is
  // the dealer now; mid-deal the deal's end advances the chair one seat,
  // so it sits one seat back until then.
  const int count = static_cast<int>(newSeats.size());
  int newDealer = dealer;
  if (dealer == seat) {
    newDealer = seat % count;
    if (phase == TablePhase::Playing) newDealer = (newDealer + count - 1) % count;
  } else if (dealer > seat) {
    newDealer--;
  }
  return TableState{std::move(newSeats),
                    std::move(newWins),
                    newDealer,
                    dealNumber,
                    phase,
                    variant,
                    std::move(newDeal),
                    gameId,
                    versionId};
}

int TableState::playerIndex(const std::string& id) const {
  for (size_t i = 0; i < seats.size(); i++) {
    if (seats[i] == id) return static_cast<int>(i);
  }
  return -1;
}

TableState TableState::withIdAndVersion(const std::string& game_id,
                                        const std::string& version_id) const {
  std::optional<Deal> stamped;
  if (deal.has_value()) {
    stamped.emplace(std::visit(
        [&](const auto& d) { return Deal(d.withIdAndVersion(game_id, version_id)); }, *deal));
  }
  return TableState{seats, wins, dealer, dealNumber, phase, variant, stamped, game_id, version_id};
}

}  // namespace rummy
