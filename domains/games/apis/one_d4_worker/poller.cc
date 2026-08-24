#include "domains/games/apis/one_d4_worker/poller.h"

#include <string>
#include <thread>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "domains/games/apis/one_d4_worker/lease_core.h"

namespace one_d4_worker {
namespace {

/// What a failed run stores in `indexing_requests.error_message`, which the
/// API hands back. A contract with the caller rather than a debugging aid:
/// one fixed sentence, never the cause. Stored rows already carry this exact
/// string, so a caller matching on it keeps matching.
constexpr char kInternalFailure[] = "Indexing failed due to an internal error";

/// A token no other claimant will present: 128 random bits.
///
/// Not a counter, because there is a poller per indexing thread and
/// their counts would start at the same place and collide — two runs
/// sharing a token both pass every fence, which is the whole thing the
/// per-run id exists to prevent. Not a timestamp either: two threads
/// claim inside one tick of any clock cheap enough to read here.
std::string RunToken() {
  thread_local absl::BitGen bitgen;
  return absl::StrCat(absl::Hex(absl::Uniform<uint64_t>(bitgen), absl::kZeroPad16),
                      absl::Hex(absl::Uniform<uint64_t>(bitgen), absl::kZeroPad16));
}

/// Holds a claim open for as long as a run needs it.
///
/// The bookkeeping is LeaseCore's; what is here is the two writes that
/// name this queue's columns.
class QueueLease : public LeaseKeeper {
 public:
  QueueLease(IndexQueue& queue, std::string id, std::string owner, absl::Duration lease,
             absl::Duration renew_every, absl::Duration max_run)
      : queue_(queue),
        id_(std::move(id)),
        owner_(std::move(owner)),
        lease_(lease),
        core_([this] { return queue_.Heartbeat({.id = id_, .owner = owner_}, lease_); }, lease,
              renew_every, max_run) {}

  bool Keep() override { return core_.Keep(); }

  bool OutOfTime() override { return core_.OutOfTime(); }

  bool Report(int games_indexed) override {
    return core_.Fenced([this, games_indexed] {
      return queue_.Progress({.id = id_, .owner = owner_}, games_indexed);
    });
  }

  bool lost() const { return core_.lost(); }

 private:
  IndexQueue& queue_;
  const std::string id_;
  const std::string owner_;
  const absl::Duration lease_;
  // Last, because its renewal thread starts here and reaches back through
  // the members above.
  LeaseCore core_;
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

absl::StatusOr<std::optional<Claim>> Poller::ClaimOne() {
  Claim claim;
  claim.owner = absl::StrCat(options_.owner, "/", RunToken());
  absl::StatusOr<std::optional<IndexJob>> claimed = queue_.ClaimNext(claim.owner, options_.lease);
  if (!claimed.ok()) return claimed.status();
  if (!claimed->has_value()) return std::nullopt;
  claim.job = **claimed;
  return claim;
}

absl::StatusOr<RunOutcome> Poller::RunClaimed(const Claim& claim) {
  const IndexJob& job = claim.job;
  const std::string& owner = claim.owner;
  QueueLease lease(queue_, job.id, owner, options_.lease, options_.renew_every, options_.max_run);
  const absl::StatusOr<RunReport> report = run_(claim, lease);

  // Ahead of the lease check, unlike everything else. A run that stopped
  // at its ceiling gives the range back even if its claim lapsed on the
  // way out: Release is fenced, so it either lands — spending the
  // attempt, which is what retires a repeatable wedge after three instead
  // of looping on it forever — or is refused because the range really did
  // change hands, and Finish calls that what it is.
  if (report.ok() && report->stopped.has_value() && *report->stopped == Stopped::kRunCeiling) {
    return Finish(RunOutcome::kInterrupted, queue_.Release(claim.ref()));
  }

  // Before anything else, including a failure: a run that lost its lease
  // reports nothing, because the row belongs to whoever holds it now.
  if (lease.lost() || (report.ok() && report->lease_lost)) return RunOutcome::kLeaseLost;

  if (!report.ok()) {
    // The cause goes to the log, not to the column. error_message is
    // handed back by the API, and a chess.com body or a libpq diagnostic
    // in there is an internal detail told to whoever asked for the index.
    LOG(ERROR) << "Run failed request_id=" << job.id << " error=" << report.status();
    return Finish(RunOutcome::kFailed, queue_.Fail(claim.ref(), kInternalFailure));
  }

  // Only kShutdown reaches here; the ceiling is handled above. The
  // request did nothing wrong, so the attempt is refunded.
  if (report->stopped.has_value()) {
    return Finish(RunOutcome::kInterrupted, queue_.HandBack(claim.ref()));
  }

  return Finish(RunOutcome::kCompleted, queue_.Complete(claim.ref(), report->games_indexed));
}

absl::StatusOr<bool> Poller::PollOnce() {
  const absl::StatusOr<std::optional<Claim>> claim = ClaimOne();
  if (!claim.ok()) return claim.status();
  if (!claim->has_value()) return false;

  const absl::StatusOr<RunOutcome> outcome = RunClaimed(**claim);
  if (!outcome.ok()) return outcome.status();
  last_outcome_ = *outcome;
  return true;
}

absl::StatusOr<RunOutcome> Poller::Finish(RunOutcome outcome, const absl::StatusOr<bool>& written) {
  if (!written.ok()) return written.status();
  // The fence said no, so the row is somebody else's and they own its
  // outcome — whatever we were about to call this run, it is theirs now.
  return *written ? outcome : RunOutcome::kLeaseLost;
}

}  // namespace one_d4_worker
