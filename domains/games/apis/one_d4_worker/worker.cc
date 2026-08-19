#include "domains/games/apis/one_d4_worker/worker.h"

#include <utility>

#include "absl/status/statusor.h"

namespace one_d4_worker {

Poller::Run MakeRun(ArchiveSource& archive, TitleRoster& titles, SinkFactory make_sink,
                    RunObserver& observer, std::function<bool()> stopping) {
  return [&archive, &titles, &observer, make_sink = std::move(make_sink),
          stopping = std::move(stopping)](const Claim& claim,
                                          LeaseKeeper& keeper) -> absl::StatusOr<RunReport> {
    const std::unique_ptr<GameSink> sink = make_sink(claim);
    IndexRun::Options options;
    options.observer = &observer;
    options.titles = &titles;
    options.stopping = stopping;
    IndexRun run(archive, *sink, options);
    return run.Execute(claim.job, keeper);
  };
}

}  // namespace one_d4_worker
