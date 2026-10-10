#include "domains/games/apis/games_hub/round_robin_wire.h"

#include <algorithm>
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

std::vector<Pairing> EffectivePairings(const HubStore::ChessEventRow& row) {
  std::vector<Pairing> pairings = row.pairings;
  std::vector<HubStore::EventGame> games = row.games;
  std::sort(games.begin(), games.end(),
            [](const auto& a, const auto& b) { return a.archive_id < b.archive_id; });
  std::vector<bool> played(pairings.size(), false);
  for (const HubStore::EventGame& game : games) {
    if (game.pairing < 0 || game.pairing >= std::ssize(pairings) || played[game.pairing]) continue;
    Pairing& pairing = pairings[game.pairing];
    std::optional<PairingResult> result;
    if (!game.winner.has_value()) {
      result = PairingResult::kDraw;
    } else if (*game.winner == pairing.white) {
      result = PairingResult::kWhite;
    } else if (*game.winner == pairing.black) {
      result = PairingResult::kBlack;
    } else {
      continue;
    }
    pairing.result = result;
    pairing.forfeit = false;
    played[game.pairing] = true;
  }
  return pairings;
}

moonbase::games::ChessRoundRobin RoundRobinOf(const HubStore::ChessEventRow& row,
                                              const std::map<int, std::string>& live) {
  const std::vector<Pairing> pairings = EffectivePairings(row);
  moonbase::games::ChessRoundRobin view;
  view.roundRobinId = row.event_id;
  view.creator = row.creator;
  view.entrants = row.entrants;
  view.terms = WireTerms(row.terms);
  for (std::size_t i = 0; i < pairings.size(); ++i) {
    const Pairing& pairing = pairings[i];
    moonbase::games::ChessPairing wire;
    wire.round = pairing.round;
    wire.white = pairing.white;
    wire.black = pairing.black;
    wire.result = ResultName(pairing.result);
    wire.forfeit = pairing.forfeit;
    wire.voided = Voided(pairing, row.withdrawn);
    if (const auto table = live.find(static_cast<int>(i)); table != live.end()) {
      wire.gameId = table->second;
    }
    view.pairings.push_back(std::move(wire));
  }
  view.withdrawn.assign(row.withdrawn.begin(), row.withdrawn.end());
  for (const Standing& standing : Standings(row.entrants, pairings, row.withdrawn)) {
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
