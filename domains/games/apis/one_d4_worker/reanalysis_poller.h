#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_POLLER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_POLLER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"
#include "domains/games/apis/one_d4_worker/reanalysis_run.h"

namespace one_d4_worker {

/// A claimed pass, and the id its writes are fenced on.
struct ReanalysisClaim {
  ReanalysisJob job;
  std::string owner;
};

/// Claims one reanalysis pass and runs it.
///
/// Terminal writes happen here and nowhere else, the same rule Poller
/// keeps for index runs.
class ReanalysisPoller {
 public:
  struct Options {
    /// Names the process. Each claim appends a random token of its own,
    /// and that is what the fencing turns on.
    std::string owner;
    absl::Duration lease = absl::Minutes(5);
    absl::Duration renew_every = absl::Minutes(5) / 4;

    /// Called once per finished pass with the outcome and this owner's
    /// share of the games — report totals minus what the claim already
    /// carried, because a resumed pass finishing under a new owner reports
    /// totals the earlier owner already counted. Unset is fine.
    std::function<void(RunOutcome, int games_processed, int games_failed)> on_finished;

    /// How long one pass may hold the row before it is treated as wedged.
    ///
    /// Survivable in a way the index ceiling is not: a pass that stops
    /// here has its cursor written, so the next claim resumes from it
    /// rather than starting the corpus again. The ceiling bounds how long
    /// one worker holds the row, not how long the corpus takes.
    absl::Duration max_run = absl::Hours(6);
  };

  using Run =
      std::function<absl::StatusOr<ReanalysisReport>(const ReanalysisClaim&, ReanalysisLease&)>;

  ReanalysisPoller(ReanalysisQueue& queue, Run run, Options options);

  absl::StatusOr<std::optional<ReanalysisClaim>> ClaimOne();

  absl::StatusOr<RunOutcome> RunClaimed(const ReanalysisClaim& claim);

  /// Claims and runs at most one pass. True when it ran one.
  absl::StatusOr<bool> PollOnce();

  RunOutcome last_outcome() const { return last_outcome_; }

 private:
  absl::StatusOr<RunOutcome> Finish(RunOutcome outcome, const absl::StatusOr<bool>& written);

  ReanalysisQueue& queue_;
  Run run_;
  Options options_;
  RunOutcome last_outcome_ = RunOutcome::kCompleted;
};

/// Claims and runs reanalysis passes until `stopping`.
///
/// One thread's worth, not a pool. A pass is a single-owner walk of the
/// whole corpus, and idx_reanalysis_requests_single_live keeps the table
/// to one live pass — so a second thread here would only ever poll an
/// empty queue.
///
/// Builds its queue through the factory on the calling thread rather than
/// taking one, because a pg::Client is one connection serialised by a
/// mutex — sharing the index pool's would put a pass's progress writes in
/// front of every indexing claim for as long as the pass runs.
///
/// `stopping` and `sleep` are called from this thread only.
void PollReanalysisUntilStopped(const std::function<std::unique_ptr<ReanalysisQueue>()>& make_queue,
                                ReanalysisPoller::Run run, ReanalysisPoller::Options poller,
                                const std::function<bool()>& stopping,
                                const std::function<void(absl::Duration)>& sleep,
                                absl::Duration idle_wait);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_POLLER_H
