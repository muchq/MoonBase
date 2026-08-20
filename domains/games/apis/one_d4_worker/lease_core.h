#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_LEASE_CORE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_LEASE_CORE_H

#include <functional>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace one_d4_worker {

/// Holding a claim open, minus anything about what is in the row.
///
/// Three rules live here, and they are the ones two pollers would get
/// wrong in two different ways if each wrote them out:
///
/// - Renewal happens on a thread, not where the run happens to check. The
///   gaps between a run's checkpoints are longer than a lease.
/// - A queue that cannot be reached is not proof the claim was lost, and
///   not proof it was kept. The benefit of the doubt runs out when the
///   lease we last proved would have expired. Writing meanwhile is safe:
///   every write is fenced on the row itself.
/// - Past the ceiling the claim stops being extended but is not disowned.
///   Not renewing recovers a wedged run, since the lease lapsing under it
///   is the only way its row comes back; disowning would abort the work in
///   hand the moment the ceiling passed.
class LeaseCore {
 public:
  /// `renew` extends the claim: true still ours, false somebody else's,
  /// error the queue could not be reached.
  LeaseCore(std::function<absl::StatusOr<bool>()> renew, absl::Duration lease,
            absl::Duration renew_every, absl::Duration max_run);
  ~LeaseCore();

  LeaseCore(const LeaseCore&) = delete;
  LeaseCore& operator=(const LeaseCore&) = delete;

  /// Whether the claim is still ours, renewing it in passing.
  bool Keep();

  bool OutOfTime() const;

  bool lost() const;

  /// Runs one fenced write and folds its answer into the same ownership
  /// bookkeeping a renewal gets — a fence that passed proves ownership as
  /// well as a renewal does, and moves lease_expires_at nowhere, so it
  /// refreshes what was last proved rather than the lease.
  bool Fenced(const std::function<absl::StatusOr<bool>()>& write);

 private:
  bool RenewLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void RenewUntilStopped();

  const std::function<absl::StatusOr<bool>()> renew_;
  const absl::Duration lease_;
  const absl::Duration renew_every_;
  const absl::Time deadline_;

  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_) = false;
  bool lost_ ABSL_GUARDED_BY(mu_) = false;
  absl::Time proven_ ABSL_GUARDED_BY(mu_);
  std::thread renewer_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_LEASE_CORE_H
