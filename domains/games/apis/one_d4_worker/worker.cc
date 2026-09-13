#include "domains/games/apis/one_d4_worker/worker.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace one_d4_worker {

Poller::Run MakeRun(PlatformArchives archives, PlatformRosters rosters, SinkFactory make_sink,
                    RunObserver& observer, std::function<bool()> stopping) {
  return [archives = std::move(archives), rosters = std::move(rosters), &observer,
          make_sink = std::move(make_sink), stopping = std::move(stopping)](
             const Claim& claim, LeaseKeeper& keeper) -> absl::StatusOr<RunReport> {
    const auto found = archives.find(claim.job.platform);
    if (found == archives.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("no archive serves platform ", claim.job.platform));
    }
    // Absent is null, and null means no roster rather than a failed one.
    // A platform that states its own titles never needed one.
    const auto roster = rosters.find(claim.job.platform);
    const std::unique_ptr<GameSink> sink = make_sink(claim);
    IndexRun::Options options;
    options.observer = &observer;
    options.titles = roster == rosters.end() ? nullptr : roster->second;
    options.stopping = stopping;
    IndexRun run(*found->second, *sink, options);
    return run.Execute(claim.job, keeper);
  };
}

std::string OwnerId(std::string_view host, int pid) {
  constexpr int kMaxHost = 40;
  return absl::StrCat("cpp/", host.substr(0, std::min<size_t>(host.size(), kMaxHost)), "/", pid);
}

}  // namespace one_d4_worker
