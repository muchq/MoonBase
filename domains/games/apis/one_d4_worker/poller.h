#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H

#include <functional>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {

/// How a run ended. The label on `index_runs`.
enum class RunOutcome { kCompleted, kFailed, kInterrupted, kLeaseLost };

std::string_view ToString(RunOutcome outcome);

/// Why a run stopped early. The two differ in what happens to the attempt.
enum class Stopped {
  /// The worker is shutting down. The request did nothing wrong, so the
  /// attempt is refunded.
  kShutdown,
  /// The run hit its own time ceiling. The attempt stays spent.
  kRunCeiling,
};

/// What a run reports back.
struct RunReport {
  int games_indexed = 0;
  /// The lease went to somebody else. They own the row's outcome now.
  bool lease_lost = false;
  std::optional<Stopped> stopped;
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
    /// How often a claim is renewed in the background, independent of what
    /// the run is doing. A quarter of the lease, so three renewals can be
    /// missed before it lapses.
    ///
    /// A run that only renewed where it happens to check would lose a
    /// month it was still working on: a month of four hundred games is
    /// four archive calls, eight hundred profile lookups and four hundred
    /// extractions between checkpoints, and the lease is five minutes.
    absl::Duration renew_every = absl::Minutes(5) / 4;
  };

  using Run = std::function<absl::StatusOr<RunReport>(const IndexJob&, LeaseKeeper&)>;

  Poller(IndexQueue& queue, Run run, Options options);

  /// Claims and runs at most one request. True when it ran one.
  absl::StatusOr<bool> PollOnce();

  RunOutcome last_outcome() const { return last_outcome_; }

 private:
  absl::StatusOr<bool> Finish(const IndexJob& job, RunOutcome outcome,
                              const absl::StatusOr<bool>& written);

  IndexQueue& queue_;
  Run run_;
  Options options_;
  RunOutcome last_outcome_ = RunOutcome::kCompleted;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
