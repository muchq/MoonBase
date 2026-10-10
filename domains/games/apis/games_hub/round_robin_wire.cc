#include "domains/games/apis/games_hub/round_robin_wire.h"

#include <optional>
#include <string>
#include <utility>

#include "domains/games/apis/games_hub/round_robin.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace games_hub {
namespace {

std::optional<std::string> ResultName(const std::optional<PairingResult>& result) {
  if (!result.has_value()) return std::nullopt;
  switch (*result) {
    case PairingResult::kWhite:
      return "white";
    case PairingResult::kBlack:
      return "black";
    case PairingResult::kDraw:
      return "draw";
  }
  return std::nullopt;
}

}  // namespace

moonbase::games::ChessTerms WireTerms(const ChessTerms& terms) {
  moonbase::games::ChessTerms wire;
  wire.setupId = terms.setup_id;
  wire.setupName = std::string(chess_play::ChessSetupName(terms.setup_id).value_or(""));
  wire.initialSeconds = static_cast<int>(terms.time_control.initial_ms / 1000);
  wire.incrementSeconds = static_cast<int>(terms.time_control.increment_ms / 1000);
  return wire;
}

moonbase::games::ChessRoundRobin RoundRobinOf(const HubStore::ChessEventRow& row) {
  moonbase::games::ChessRoundRobin view;
  view.roundRobinId = row.event_id;
  view.creator = row.creator;
  view.entrants = row.entrants;
  view.terms = WireTerms(row.terms);
  for (const Pairing& pairing : row.pairings) {
    moonbase::games::ChessPairing wire;
    wire.round = pairing.round;
    wire.white = pairing.white;
    wire.black = pairing.black;
    wire.result = ResultName(pairing.result);
    wire.forfeit = pairing.forfeit;
    wire.voided = Voided(pairing, row.withdrawn);
    view.pairings.push_back(std::move(wire));
  }
  view.withdrawn.assign(row.withdrawn.begin(), row.withdrawn.end());
  for (const Standing& standing : Standings(row.entrants, row.pairings, row.withdrawn)) {
    moonbase::games::ChessStanding wire;
    wire.playerId = standing.player;
    wire.points = standing.points;
    wire.sonnebornBerger = standing.sonneborn_berger;
    wire.place = standing.place;
    wire.withdrawn = standing.withdrawn;
    view.standings.push_back(std::move(wire));
  }
  return view;
}

}  // namespace games_hub
