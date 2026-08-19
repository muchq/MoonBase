#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H

#include <functional>
#include <memory>

#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/title_roster.h"

namespace one_d4_worker {

/// Builds the sink for one claimed request. A sink carries the job's id
/// and the owner it must fence on, so there is one per run and not one
/// per process.
using SinkFactory = std::function<std::unique_ptr<GameSink>(const IndexJob&)>;

/// What the poller calls with each claimed request.
///
/// The roster arrives by reference and is never built in here. That is
/// the whole difference between ten requests to chess.com for the life of
/// the process and ten per claim, and it is a difference nothing else
/// would notice: a per-claim roster answers every question correctly and
/// only costs.
Poller::Run MakeRun(ArchiveSource& archive, TitleRoster& titles, SinkFactory make_sink,
                    RunObserver& observer, std::function<bool()> stopping);

/// Claims and runs requests until `stopping`.
///
/// `sleep` is how long it waits before asking an empty or unreachable
/// queue again, injected so a test does not have to.
void PollLoop(Poller& poller, WorkerMetrics& metrics, absl::Duration idle_wait,
              const std::function<bool()>& stopping,
              const std::function<void(absl::Duration)>& sleep);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_WORKER_H
