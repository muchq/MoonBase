#include "domains/games/apis/one_d4_worker/worker.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"

namespace one_d4_worker {

Poller::Run MakeRun(ArchiveSource& archive, TitleRoster& titles, SinkFactory make_sink,
                    RunObserver& observer, std::function<bool()> stopping) {
  return [&archive, &titles, &observer, make_sink = std::move(make_sink),
          stopping = std::move(stopping)](const IndexJob& job,
                                          LeaseKeeper& keeper) -> absl::StatusOr<RunReport> {
    const std::unique_ptr<GameSink> sink = make_sink(job);
    IndexRun::Options options;
    options.observer = &observer;
    options.titles = &titles;
    options.stopping = stopping;
    IndexRun run(archive, *sink, options);
    return run.Execute(job, keeper);
  };
}

void PollLoop(Poller& poller, WorkerMetrics& metrics, absl::Duration idle_wait,
              const std::function<bool()>& stopping,
              const std::function<void(absl::Duration)>& sleep) {
  while (!stopping()) {
    const absl::Time started = absl::Now();
    const absl::StatusOr<bool> ran = poller.PollOnce();
    if (!ran.ok()) {
      // A queue we cannot reach is a reason to wait and try again, not to
      // exit: the supervisor would only restart us into the same outage.
      LOG(ERROR) << "Poll failed: " << ran.status();
      sleep(idle_wait);
      continue;
    }
    if (!*ran) {
      sleep(idle_wait);
      continue;
    }
    metrics.RunFinished(poller.last_outcome(), absl::Now() - started);
    LOG(INFO) << "Run finished: " << ToString(poller.last_outcome());
  }
}

}  // namespace one_d4_worker
