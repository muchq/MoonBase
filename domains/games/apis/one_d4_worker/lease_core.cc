#include "domains/games/apis/one_d4_worker/lease_core.h"

#include <utility>

namespace one_d4_worker {

LeaseCore::LeaseCore(std::function<absl::StatusOr<bool>()> renew, absl::Duration lease,
                     absl::Duration renew_every, absl::Duration max_run)
    : renew_(std::move(renew)),
      lease_(lease),
      renew_every_(renew_every),
      deadline_(absl::Now() + max_run),
      proven_(absl::Now()) {
  renewer_ = std::thread([this] { RenewUntilStopped(); });
}

LeaseCore::~LeaseCore() {
  {
    const absl::MutexLock lock(&mu_);
    stop_ = true;
  }
  renewer_.join();
}

bool LeaseCore::Keep() {
  const absl::MutexLock lock(&mu_);
  if (lost_) return false;
  return RenewLocked();
}

bool LeaseCore::OutOfTime() const {
  const absl::MutexLock lock(&mu_);
  return absl::Now() >= deadline_;
}

bool LeaseCore::lost() const {
  const absl::MutexLock lock(&mu_);
  return lost_;
}

bool LeaseCore::Fenced(const std::function<absl::StatusOr<bool>()>& write) {
  const absl::MutexLock lock(&mu_);
  if (lost_) return false;
  const absl::StatusOr<bool> recorded = write();
  if (recorded.ok() && *recorded) {
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

bool LeaseCore::RenewLocked() {
  // Past the ceiling the claim stops being *extended* — which is what
  // recovers a run wedged inside a single unit of work, since it will
  // never reach the check between units and the only way its row comes
  // back is the lease lapsing under it.
  //
  // Not renewing is not the same as not owning, though, and answering
  // false here for both would abort the work in hand the moment the
  // ceiling passed. We still hold the claim until the lease we last
  // proved runs out.
  if (absl::Now() >= deadline_) {
    if (absl::Now() - proven_ >= lease_) lost_ = true;
    return !lost_;
  }
  const absl::StatusOr<bool> held = renew_();
  if (held.ok() && *held) {
    proven_ = absl::Now();
    return true;
  }
  // The queue answered, and the answer is no: somebody else holds this.
  if (held.ok()) {
    lost_ = true;
    return false;
  }
  if (absl::Now() - proven_ >= lease_) lost_ = true;
  return !lost_;
}

void LeaseCore::RenewUntilStopped() {
  const absl::MutexLock lock(&mu_);
  while (!mu_.AwaitWithTimeout(absl::Condition(&stop_), renew_every_)) {
    if (lost_) return;
    RenewLocked();
  }
}

}  // namespace one_d4_worker
