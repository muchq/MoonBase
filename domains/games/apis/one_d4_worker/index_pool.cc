#include "domains/games/apis/one_d4_worker/index_pool.h"

#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"

namespace one_d4_worker {

IndexPool::IndexPool(Poller& poller, WorkerMetrics& metrics, Options options)
    : poller_(poller), metrics_(metrics), options_(std::move(options)) {}

void IndexPool::Run(const std::function<bool()>& stopping,
                    const std::function<void(absl::Duration)>& sleep) {
  std::vector<std::thread> workers;
  workers.reserve(options_.slots);
  for (int slot = 0; slot < options_.slots; ++slot) workers.emplace_back([this] { Work(); });

  while (!stopping()) {
    if (!TakeSlot(stopping)) break;

    const absl::StatusOr<std::optional<Claim>> claim = poller_.ClaimOne();
    if (!claim.ok()) {
      // A queue we cannot reach is a reason to wait and try again, not to
      // exit: the supervisor would only restart us into the same outage.
      LOG(ERROR) << "Claim failed: " << claim.status();
      GiveBackSlot();
      sleep(options_.idle_wait);
      continue;
    }
    if (!claim->has_value()) {
      GiveBackSlot();
      sleep(options_.idle_wait);
      continue;
    }
    Dispatch(**claim);
  }

  Close();
  for (std::thread& worker : workers) worker.join();
}

bool IndexPool::TakeSlot(const std::function<bool()>& stopping) {
  while (!stopping()) {
    const absl::MutexLock lock(mu_);
    // Timed rather than waited outright, because the thing that ends the
    // wait is a shutdown flag no run touches — nothing here would ever
    // signal it.
    if (mu_.AwaitWithTimeout(absl::Condition(this, &IndexPool::HasFreeSlot),
                             absl::Milliseconds(50))) {
      ++busy_;
      return true;
    }
  }
  return false;
}

void IndexPool::GiveBackSlot() {
  const absl::MutexLock lock(mu_);
  --busy_;
}

void IndexPool::Dispatch(Claim claim) {
  const absl::MutexLock lock(mu_);
  pending_.push_back(std::move(claim));
}

void IndexPool::Close() {
  const absl::MutexLock lock(mu_);
  closed_ = true;
}

void IndexPool::Work() {
  while (true) {
    Claim claim;
    {
      const absl::MutexLock lock(mu_);
      mu_.Await(absl::Condition(this, &IndexPool::HasWorkOrClosed));
      // Closed and drained. A claim already dispatched is still run:
      // handing it back is what the run itself does on shutdown.
      if (pending_.empty()) return;
      claim = std::move(pending_.front());
      pending_.pop_front();
    }

    const absl::Time started = absl::Now();
    const absl::StatusOr<RunOutcome> outcome = poller_.RunClaimed(claim);
    if (outcome.ok()) {
      metrics_.RunFinished(*outcome, absl::Now() - started);
      LOG(INFO) << "Run finished: " << ToString(*outcome);
    } else {
      // The run is over either way; the row expires and somebody retries.
      LOG(ERROR) << "Could not write the outcome of " << claim.job.id << ": " << outcome.status();
    }

    const absl::MutexLock lock(mu_);
    --busy_;
  }
}

}  // namespace one_d4_worker
