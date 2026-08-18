#include "domains/games/apis/one_d4_worker/poller.h"

#include <string>
#include <utility>

#include "absl/status/status.h"

namespace one_d4_worker {
namespace {

/// Renews through the queue, and remembers a loss so a run cannot keep
/// asking after the answer has become no.
class QueueLease : public LeaseKeeper {
 public:
  QueueLease(IndexQueue& queue, std::string id, std::string owner, absl::Duration lease)
      : queue_(queue), id_(std::move(id)), owner_(std::move(owner)), lease_(lease) {}

  bool Keep() override {
    if (lost_) return false;
    const absl::StatusOr<bool> held = queue_.Heartbeat(id_, owner_, lease_);
    // A queue we cannot reach is not a lease we can prove we hold.
    lost_ = !held.ok() || !*held;
    return !lost_;
  }

  bool lost() const { return lost_; }

 private:
  IndexQueue& queue_;
  std::string id_;
  std::string owner_;
  absl::Duration lease_;
  bool lost_ = false;
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
  QueueLease lease(queue_, job.id, options_.owner, options_.lease);
  const absl::StatusOr<RunReport> report = run_(job, lease);

  if (!report.ok()) {
    last_outcome_ = RunOutcome::kFailed;
    const absl::StatusOr<bool> failed =
        queue_.Fail(job.id, options_.owner, report.status().message());
    if (!failed.ok()) return failed.status();
    return true;
  }

  // A run that lost its lease reports nothing: the row belongs to whoever
  // holds it now, and they will write its outcome.
  if (report->lease_lost || lease.lost()) {
    last_outcome_ = RunOutcome::kLeaseLost;
    return true;
  }

  // Interrupted is a shutdown, not a verdict on the request. Leaving the
  // row alone lets the lease expire and somebody else pick it up, without
  // spending one of its attempts.
  if (report->interrupted) {
    last_outcome_ = RunOutcome::kInterrupted;
    return true;
  }

  last_outcome_ = RunOutcome::kCompleted;
  const absl::StatusOr<bool> completed =
      queue_.Complete(job.id, options_.owner, report->games_indexed);
  if (!completed.ok()) return completed.status();
  return true;
}

}  // namespace one_d4_worker
