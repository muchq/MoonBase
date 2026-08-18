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

  // Before anything else, including a failure: a run that lost its lease
  // reports nothing, because the row belongs to whoever holds it now.
  if (lease.lost() || (report.ok() && report->lease_lost)) {
    last_outcome_ = RunOutcome::kLeaseLost;
    return true;
  }

  if (!report.ok()) {
    return Finish(job, RunOutcome::kFailed,
                  queue_.Fail(job.id, options_.owner, report.status().message()));
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
