#include "domains/games/apis/games_hub/rummy_adapter.h"

namespace games_hub {
namespace {

// A deal's finish (#1609): each deal is a game — the seat that went out
// won it and everyone dealt in played it. A leaver is compacted out of the
// engine's seats, as in castle. No score: rummy's points count up for the
// winner where golf's count against every seat, so adding them to the
// room's running total would mix two scales; the points ride the view's
// lastDeal instead.
StatsDeltas DealDeltas(const rummy::Deal& deal) {
  std::vector<std::string> dealt;
  for (const rummy::Player& seat : rummy::dealPlayers(deal)) dealt.push_back(seat.id);
  return PlayedWon(dealt, rummy::dealWinner(deal).value_or(""));
}

}  // namespace

std::string Adapter<rummy::TableState>::Phase(const rummy::TableState& table) {
  switch (table.getPhase()) {
    case rummy::TablePhase::Choosing:
      return "choosing";
    case rummy::TablePhase::Playing:
      return "playing";
    case rummy::TablePhase::Closed:
      break;
  }
  return "ended";
}

std::string Adapter<rummy::TableState>::Turn(const rummy::TableState& table) {
  // Between deals nobody has a turn: the dealer's choice is not one.
  if (table.getPhase() != rummy::TablePhase::Playing) return "";
  const int seat = rummy::dealWhoseTurn(*table.getDeal());
  return seat < 0 ? "" : rummy::dealPlayers(*table.getDeal()).at(seat).id;
}

// A table closes by a leave. A deal it broke up was played by those still
// dealt in; one that had already ended was credited as it did.
StatsDeltas Adapter<rummy::TableState>::ClosedDeltas(const rummy::TableState& table,
                                                     const std::vector<std::string>&) {
  const auto& deal = table.getDeal();
  if (deal.has_value() && rummy::dealPhase(*deal) == rummy::Phase::Abandoned) {
    return DealDeltas(*deal);
  }
  return {};
}

// A deal won by play: the table goes back to choosing.
std::optional<StatsDeltas> Adapter<rummy::TableState>::DealEndDeltas(
    const rummy::TableState& before, const rummy::TableState& after) {
  if (before.getPhase() != rummy::TablePhase::Playing ||
      after.getPhase() != rummy::TablePhase::Choosing) {
    return std::nullopt;
  }
  return DealDeltas(*after.getDeal());
}

}  // namespace games_hub
