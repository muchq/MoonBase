#include "domains/games/apis/one_d4_worker/poller.h"

#include <string>
#include <thread>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"

namespace one_d4_worker {
namespace {

/// What a failed run stores in `indexing_requests.error_message`, which
/// the API returns. Verbatim from the Java worker, so the two report a
/// failure the same way.
constexpr char kInternalFailure[] = "Indexing failed due to an internal error";

/// Holds a claim open for as long as a run needs it.
///
/// Renews on its own thread rather than only where the run asks, because
/// the gaps between a run's checkpoints are longer than a lease. Keep() is
/// then a question — do we still hold this — and not the thing keeping it
/// alive.
class QueueLease : public LeaseKeeper {
 public:
  QueueLease(IndexQueue& queue, std::string id, std::string owner, absl::Duration lease,
             absl::Duration renew_every)
      : queue_(queue),
        id_(std::move(id)),
        owner_(std::move(owner)),
        lease_(lease),
        renew_every_(renew_every),
        proven_(absl::Now()) {
    renewer_ = std::thread([this] { RenewUntilStopped(); });
  }

  ~QueueLease() override {
    {
      const absl::MutexLock lock(&mu_);
      stop_ = true;
    }
    renewer_.join();
  }

  QueueLease(const QueueLease&) = delete;
  QueueLease& operator=(const QueueLease&) = delete;

  bool Keep() override {
    const absl::MutexLock lock(&mu_);
    if (lost_) return false;
    return RenewLocked();
  }

  bool Report(int games_indexed) override {
    const absl::MutexLock lock(&mu_);
    if (lost_) return false;
    const absl::StatusOr<bool> recorded = queue_.Progress(id_, owner_, games_indexed);
    if (recorded.ok() && *recorded) {
      // A fence that passed is proof of ownership as good as a renewal's,
      // and it moved lease_expires_at nowhere — so it refreshes what we
      // last proved, not the lease itself.
      proven_ = absl::Now();
      return true;
    }
    if (recorded.ok()) {
      lost_ = true;
      return false;
    }
    // Same benefit of the doubt a renewal gets, and for the same reason.
    if (absl::Now() - proven_ >= lease_) lost_ = true;
    return !lost_;
  }

  bool lost() const {
    const absl::MutexLock lock(&mu_);
    return lost_;
  }

 private:
  bool RenewLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    const absl::StatusOr<bool> held = queue_.Heartbeat(id_, owner_, lease_);
    if (held.ok() && *held) {
      proven_ = absl::Now();
      return true;
    }
    // The queue answered, and the answer is no: somebody else holds this.
    if (held.ok()) {
      lost_ = true;
      return false;
    }
    // A queue we cannot reach is not proof we lost the claim — and giving
    // up on the first blip abandons a run nobody else wants, mid-way. It
    // is not proof we still hold it either, so the benefit of the doubt
    // runs out when the lease we last proved would have expired. Writing
    // meanwhile is safe: every write is fenced on the row itself.
    if (absl::Now() - proven_ >= lease_) lost_ = true;
    return !lost_;
  }

  void RenewUntilStopped() {
    const absl::MutexLock lock(&mu_);
    while (!mu_.AwaitWithTimeout(absl::Condition(&stop_), renew_every_)) {
      if (lost_) return;
      RenewLocked();
    }
  }

  IndexQueue& queue_;
  const std::string id_;
  const std::string owner_;
  const absl::Duration lease_;
  const absl::Duration renew_every_;

  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_) = false;
  bool lost_ ABSL_GUARDED_BY(mu_) = false;
  absl::Time proven_ ABSL_GUARDED_BY(mu_);
  std::thread renewer_;
};

}  // namespace

std::string_view ToString(RunOutcome outcome) {
  switch (outcome) {
    case RunOutcome::kCompleted:
      return "completed";
    case RunOutcome::kFailed:
      return "failed";
    case RunOutcome::kInterrupted:
      return "interrupted";
    case RunOutcome::kLeaseLost:
      return "lease_lost";
  }
  return "unknown";
}

Poller::Poller(IndexQueue& queue, Run run, Options options)
    : queue_(queue), run_(std::move(run)), options_(std::move(options)) {}

absl::StatusOr<bool> Poller::PollOnce() {
  absl::StatusOr<std::optional<IndexJob>> claimed =
      queue_.ClaimNext(options_.owner, options_.lease);
  if (!claimed.ok()) return claimed.status();
  if (!claimed->has_value()) return false;

  const IndexJob& job = **claimed;
  QueueLease lease(queue_, job.id, options_.owner, options_.lease, options_.renew_every);
  const absl::StatusOr<RunReport> report = run_(job, lease);

  // Before anything else, including a failure: a run that lost its lease
  // reports nothing, because the row belongs to whoever holds it now.
  if (lease.lost() || (report.ok() && report->lease_lost)) {
    last_outcome_ = RunOutcome::kLeaseLost;
    return true;
  }

  if (!report.ok()) {
    // The cause goes to the log, not to the column. error_message is
    // handed back by the API, and a chess.com body or a libpq diagnostic
    // in there is an internal detail told to whoever asked for the index.
    LOG(ERROR) << "Indexing request " << job.id << " failed: " << report.status();
    return Finish(job, RunOutcome::kFailed, queue_.Fail(job.id, options_.owner, kInternalFailure));
  }

  if (report->stopped.has_value()) {
    const absl::StatusOr<bool> handed = *report->stopped == Stopped::kShutdown
                                            ? queue_.HandBack(job.id, options_.owner)
                                            : queue_.Release(job.id, options_.owner);
    return Finish(job, RunOutcome::kInterrupted, handed);
  }

  return Finish(job, RunOutcome::kCompleted,
                queue_.Complete(job.id, options_.owner, report->games_indexed));
}

absl::StatusOr<bool> Poller::Finish(const IndexJob& job, RunOutcome outcome,
                                    const absl::StatusOr<bool>& written) {
  if (!written.ok()) return written.status();
  // The fence said no, so the row is somebody else's and they own its
  // outcome — whatever we were about to call this run, it is theirs now.
  last_outcome_ = *written ? outcome : RunOutcome::kLeaseLost;
  return true;
}

}  // namespace one_d4_worker
