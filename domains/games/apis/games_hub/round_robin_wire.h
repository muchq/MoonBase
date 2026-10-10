#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_WIRE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_WIRE_H

#include <map>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/round_robin.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// A challenge's or a round robin's terms as the wire spells them: the
/// setup and its display name, the clock in seconds.
moonbase::games::ChessTerms WireTerms(const ChessTerms& terms);

/// A round robin as every member sees it (#1647): its pairings with their
/// results and whether each is void, and the standings round_robin.h
/// ranks.
/// `live` names the table playing each pairing now, by its index.
moonbase::games::ChessRoundRobin RoundRobinOf(const HubStore::ChessEventRow& row,
                                              const std::map<int, std::string>& live = {});

/// The row's pairings with the games played for them (#1647): a pairing's
/// first archived game is its result, over a forfeit the creator recorded
/// and over a later game from a table that raced it. A game naming no
/// pairing, or a winner who isn't in it, decides nothing.
std::vector<Pairing> EffectivePairings(const HubStore::ChessEventRow& row);

}  // namespace games_hub

#endif
