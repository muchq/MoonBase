#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H

#include <functional>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/claim_ref.h"
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

/// A run's handle to the claim it is working under.
class LeaseKeeper {
 public:
  virtual ~LeaseKeeper() = default;

  /// Whether the claim is still ours. False once it is lost — stop and
  /// report it.
  virtual bool Keep() = 0;

  /// Records how many games have been indexed so far, so a long backfill
  /// shows progress instead of sitting at zero until it ends. Same answer
  /// as Keep(), from the same fence, so a refusal is a lost claim.
  virtual bool Report(int games_indexed) = 0;

  /// True once the run has been going longer than any legitimate run.
  ///
  /// The wedge ceiling. A run past it may finish the month it is already
  /// inside — the games are extracted either way — but may not start
  /// another, and its claim stops being renewed so a replacement can take
  /// the range even if this run never returns at all.
  virtual bool OutOfTime() = 0;
};

/// A claim, and the id it is fenced on.
struct Claim {
  IndexJob job;
  std::string owner;

  ClaimRef ref() const { return {.id = job.id, .owner = owner}; }
};

/// Claims one request and runs it.
///
/// Terminal writes happen here and nowhere else, so the rule that a run
/// which lost its lease reports nothing lives in one place.
class Poller {
 public:
  struct Options {
    /// Names the process, for whoever reads the column. Each claim
    /// appends a random token of its own, and that is what the fencing
    /// turns on: an id is a token per run and not per worker, because
    /// reclaiming a row under the id already on it spends no attempt —
    /// right when the run holding it has ended, wrong when another run
    /// of this process is still wedged on it.
    std::string owner;
    /// How long a claim is good for without renewal. See max_run below for
    /// where this and the two after it come from in production.
    absl::Duration lease = absl::Minutes(5);

    /// How often a claim is renewed in the background, independent of what
    /// the run is doing. A quarter of the lease, so three renewals can be
    /// missed before it lapses.
    ///
    /// A run that only renewed where it happens to check would lose a
    /// month it was still working on: a month of four hundred games is an
    /// archive call and four hundred PGN replays between checkpoints, and
    /// the lease is five minutes.
    absl::Duration renew_every = absl::Minutes(5) / 4;

    /// How long a run may hold a range before it is treated as wedged.
    ///
    /// Production does not use this default, nor the two above:
    /// PollerOptionsFrom fills all three from retention_policy.json, the
    /// shared file the Java service reads too. They are fallbacks for tests
    /// that construct an Options directly and do not care about the value —
    /// the ones that do care set their own, at millisecond scale.
    ///
    /// Deliberately far above any legitimate run — a twelve-month range
    /// for a prolific player is minutes against a healthy chess.com — so
    /// that crossing it is evidence of a fault rather than of a big
    /// request. The cost of being wrong is asymmetric: too high only
    /// delays recovering from a wedge, while too low cuts real work short
    /// and spends an attempt doing it, three of which retire the request
    /// with a message blaming the range.
    absl::Duration max_run = absl::Hours(6);
  };

  /// Runs one claimed request. Given the whole claim, not just the job,
  /// because the run's writes are fenced on the id it was claimed under
  /// and that id is minted per claim.
  using Run = std::function<absl::StatusOr<RunReport>(const Claim&, LeaseKeeper&)>;

  Poller(IndexQueue& queue, Run run, Options options);

  /// Claims at most one request. nullopt when there was nothing to take.
  ///
  /// Separate from running it because the pool claims on one thread and
  /// runs on another — and because claiming must be gated on a free slot,
  /// which the caller knows about and this does not. A claim waiting for
  /// a thread is a lease renewed for a range nobody is indexing.
  absl::StatusOr<std::optional<Claim>> ClaimOne();

  /// Runs a claim and writes its outcome. Errors are the queue refusing
  /// the terminal write, not the run failing — a failed run is an
  /// outcome.
  absl::StatusOr<RunOutcome> RunClaimed(const Claim& claim);

  /// Claims and runs at most one request. True when it ran one.
  absl::StatusOr<bool> PollOnce();

  RunOutcome last_outcome() const { return last_outcome_; }

 private:
  absl::StatusOr<RunOutcome> Finish(RunOutcome outcome, const absl::StatusOr<bool>& written);

  IndexQueue& queue_;
  Run run_;
  Options options_;
  RunOutcome last_outcome_ = RunOutcome::kCompleted;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_H
