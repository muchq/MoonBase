#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/hub_store.h"
#include "moonbase/games/server.h"

namespace games_hub {

using StatsDeltas = std::vector<HubStore::StatsDelta>;

/// Events for their recipients, in the order each must observe them.
using Staged = std::vector<std::pair<std::string, moonbase::games::GameEvents>>;

/// What a table's views read besides its engine, as the hub holds it for
/// one call. A view is a pure function of this, the engine (null before
/// the table starts) and its viewer.
struct TableContext {
  const std::string& game_id;
  const std::vector<std::string>& roster;
  /// Members watching from no seat; only a kWatchable table has any.
  const std::set<std::string>& watchers;
  const std::optional<ChessTerms>& terms;
  const std::optional<EventTag>& event;
  /// Epoch milliseconds on the chess clock.
  int64_t now_ms = 0;
};

/// The shared machinery a game opts into, as Adapter::kCapabilities.
enum Capability : unsigned {
  /// One view serves every seat, and members may watch from no seat.
  kWatchable = 1U << 0,
};

/// A game's side of the hub (#1621), one specialization per engine in the
/// game's own <game>_adapter.h. adapters.h dispatches on HostedState and
/// GameKind, and holds every specialization to the hooks below, so an
/// engine without one does not compile. Each specialization holds:
///
///   Engine, Move, Update, View
///                           the engine, its envelope's command and update
///                           unions, and the view a seat sees
///   kKind, kMaxSeats, kCapabilities
///   kCommandSeries, kCommands, kEventSeries, kEvents
///                           the counter series its envelope counts on, and
///                           the union cases each is declared with
///   MoveOf(commands), UpdateOf(events)
///                           its envelope's union, if the stream's holds it
///   Wrap(update)            an update in its envelope
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
///   ViewOf(context, engine, viewer)
///                           what `viewer` sees of the table; a null engine
///                           is a table waiting to start
///   StateEvent(view), JoinedEvent(view)
///                           a view as gameState and as gameJoined
///   GamesEnded(engine, told)
///                           gameEnded for each game past the first `told`
///   ClosedEvent(engine, roster)
///                           what the seats hear after the final views of a
///                           closed table, if anything
template <typename Engine>
struct Adapter;

/// Everyone in `players` played, and `winner` won ("" for nobody).
StatsDeltas PlayedWon(const std::vector<std::string>& players, std::string_view winner);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
