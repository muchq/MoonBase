#include "domains/games/apis/one_d4_worker/worker.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

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

std::string OwnerId(std::string_view host, int pid) {
  constexpr int kMaxHost = 40;
  return absl::StrCat("cpp/", host.substr(0, std::min<size_t>(host.size(), kMaxHost)), "/", pid);
}

}  // namespace one_d4_worker
