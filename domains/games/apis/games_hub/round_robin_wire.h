#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_WIRE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_WIRE_H

#include "domains/games/apis/games_hub/hub_store.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// A challenge's or a round robin's terms as the wire spells them: the
/// setup and its display name, the clock in seconds.
moonbase::games::ChessTerms WireTerms(const ChessTerms& terms);

/// A round robin as every member sees it (#1647): its pairings with their
/// results and whether each is void, and the standings round_robin.h
/// ranks.
moonbase::games::ChessRoundRobin RoundRobinOf(const HubStore::ChessEventRow& row);

}  // namespace games_hub

#endif
