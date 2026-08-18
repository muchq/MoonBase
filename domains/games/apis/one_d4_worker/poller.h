#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H

#include <functional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {

/// How a run ended. The label on `index_runs`.
enum class RunOutcome { kCompleted, kFailed, kInterrupted, kLeaseLost };

std::string_view ToString(RunOutcome outcome);

/// What a run reports back.
struct RunReport {
  int games_indexed = 0;
  /// The run stopped because the lease went to somebody else. They own the
  /// row's outcome now.
  bool lease_lost = false;
  /// The worker is shutting down. Not a failure of the request.
  bool interrupted = false;
};

/// Keeps a claim alive while a run works.
class LeaseKeeper {
 public:
  virtual ~LeaseKeeper() = default;
  /// Extends the lease. False once it is lost — stop and report it.
  virtual bool Keep() = 0;
};

/// Claims one request and runs it.
///
/// Terminal writes happen here and nowhere else, so the rule that a run
/// which lost its lease reports nothing lives in one place.
class Poller {
 public:
  struct Options {
    std::string owner;
    absl::Duration lease = absl::Minutes(5);
  };

  using Run = std::function<absl::StatusOr<RunReport>(const IndexJob&, LeaseKeeper&)>;

  Poller(IndexQueue& queue, Run run, Options options);

  /// Claims and runs at most one request. True when it ran one.
  absl::StatusOr<bool> PollOnce();

  RunOutcome last_outcome() const { return last_outcome_; }

 private:
  IndexQueue& queue_;
  Run run_;
  Options options_;
  RunOutcome last_outcome_ = RunOutcome::kCompleted;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
