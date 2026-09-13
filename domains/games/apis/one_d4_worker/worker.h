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

/// The spelling the API stores in {@code indexing_requests.platform}:
/// uppercased with dots as underscores, so "chess.com" is "CHESS_COM".
///
/// IndexRequestService.canonicalPlatform is what actually writes the column;
/// this mirrors that rule so a registry keyed either way answers a claimed
/// row. The two are not compiled together, so changing one means changing
/// both.
std::string CanonicalPlatform(std::string_view platform);

/// Which archive serves which platform. Keys are canonicalised on lookup,
/// so registering under either spelling works.
using PlatformArchives = absl::flat_hash_map<std::string, ArchiveSource*>;

/// What the poller calls with each claimed request.
///
/// The archive is chosen per job, because a request names the platform it
/// is for. A platform no archive serves fails the run: returning ok having
/// read nothing would complete the request and cache every month in it as
/// empty, which is #1360's rule one level up.
///
/// The roster arrives by reference and is never built in here. That is
/// the whole difference between ten requests to chess.com for the life of
/// the process and ten per claim, and it is a difference nothing else
/// would notice: a per-claim roster answers every question correctly and
/// only costs.
Poller::Run MakeRun(PlatformArchives archives, TitleRoster& titles, SinkFactory make_sink,
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
