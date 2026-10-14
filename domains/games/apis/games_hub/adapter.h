#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/hub_store.h"

namespace games_hub {

using StatsDeltas = std::vector<HubStore::StatsDelta>;

/// A game's side of the hub (#1621), one specialization per engine in the
/// game's own <game>_adapter.h. adapters.h dispatches on HostedState, so an
/// engine without one does not compile. Each specialization holds:
///
///   kKind, kMaxSeats
///   Phase(engine)           the status the room lists the table under
///   Turn(engine)            the occupant to move, or ""
///   GamesFinished(engine)   games a multi-game table (chess, pong, spades)
///                           has ended, 0 otherwise: the replay cursor of a
///                           table an instance takes on
///   ClosedDeltas(engine, roster)
///                           what a finish that closes the table credits
///   DealEndDeltas(before, after)
///                           what a transition that ends a game without
///                           closing the table credits, if it did
///   DealStarted(before, after)
///                           whether a transition began the table's next game
///   WithoutSeat(engine, seat, now_ms)
///                           the engine once `seat` leaves
template <typename Engine>
struct Adapter;

/// Everyone in `players` played, and `winner` won ("" for nobody).
StatsDeltas PlayedWon(const std::vector<std::string>& players, std::string_view winner);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
