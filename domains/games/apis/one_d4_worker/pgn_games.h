#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PGN_GAMES_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PGN_GAMES_H

#include <string>
#include <string_view>
#include <vector>

#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/libs/chess_cpp/pgn.h"

namespace one_d4_worker {

/// Splits concatenated PGN into one string per game, each holding the text
/// it arrived as.
///
/// Separate because the parser hands back tags and moves rather than
/// source: the row stores the PGN itself, so something has to keep the
/// bytes. Games are cut at a line beginning "[Event ", the one tag every
/// game opens with.
std::vector<std::string_view> SplitPgnGames(std::string_view pgn);

/// The speed a platform reads off a game's tags; "" when they do not say.
using TimeClassRule = std::string (*)(const chess_cpp::Headers& headers);

/// One game of a PGN export as the row wants it, read from its own tags.
///
/// A block that will not parse is still a game, with only `pgn` set, and
/// the run already has a policy for one: IndexRun writes the row with no
/// moves rather than dropping it. Skipping here would pre-empt that — and
/// if every block failed, the month would come back an empty success and
/// be recorded complete, which is the quiet-month lie #1360 exists to
/// prevent.
ArchivedGame ArchivedGameFromPgn(std::string_view block, TimeClassRule time_class);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PGN_GAMES_H
