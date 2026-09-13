#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/title_roster.h"

namespace one_d4_worker {

/// Builds the sink for one claimed request. A sink carries the job's id
/// and the owner it must fence on — both per claim, so there is one sink
/// per run and not one per process.
using SinkFactory = std::function<std::unique_ptr<GameSink>(const Claim&)>;

/// Which archive serves which platform.
///
/// Keys are the spelling stored in indexing_requests.platform, which
/// IndexRequestService normalises before the row exists — CHESS_COM, not
/// chess.com. Matched exactly: the stored form is the contract, and a
/// registry that also answered friendlier spellings would hide a key that
/// no row will ever carry.
using PlatformArchives = absl::flat_hash_map<std::string, ArchiveSource*>;

/// Which roster answers for which platform, keyed the same way.
///
/// A platform absent from here has no roster, which is not a roster that
/// failed to load: Lichess states a title on the game itself and has no
/// roster endpoint to read. A month indexed without one is complete.
///
/// Separate maps rather than one entry per platform because the two are
/// not paired: every platform has an archive, and only chess.com has a
/// roster.
using PlatformRosters = absl::flat_hash_map<std::string, TitleRoster*>;

/// What the poller calls with each claimed request.
///
/// The archive is chosen per job, because a request names the platform it
/// is for. A platform no archive serves fails the run: returning ok having
/// read nothing would complete the request and cache every month in it as
/// empty, which is #1360's rule one level up.
///
/// The roster is chosen per job too, and for the same reason the archive
/// is: a username means a different player on another platform. One roster
/// serving both would let chess.com's GM list title a Lichess player who
/// merely shares the handle — the mirror of the bug that keys player_titles
/// on (platform, username).
///
/// Rosters arrive by reference and are never built in here. That is the
/// whole difference between ten requests to chess.com for the life of the
/// process and ten per claim, and it is a difference nothing else would
/// notice: a per-claim roster answers every question correctly and only
/// costs.
Poller::Run MakeRun(PlatformArchives archives, PlatformRosters rosters, SinkFactory make_sink,
                    RunObserver& observer, std::function<bool()> stopping);

/// Names this process in the owner column, for whoever reads it while
/// debugging a stuck range.
///
/// Only has to be readable, not unique: each claim appends a random
/// token and that is what the fencing turns on (see poller.h). The host
/// is truncated because owner_id is VARCHAR(128) and the claim appends
/// 33 characters — a long hostname would otherwise fail every claim
/// outright rather than merely reading badly.
std::string OwnerId(std::string_view host, int pid);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H
