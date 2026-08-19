#include "domains/games/apis/one_d4_worker/index_pool.h"

#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"

namespace one_d4_worker {

IndexPool::IndexPool(QueueFactory make_queue, Poller::Run run, Poller::Options poller,
                     WorkerMetrics& metrics, Options options)
    : make_queue_(std::move(make_queue)),
      run_(std::move(run)),
      poller_(std::move(poller)),
      metrics_(metrics),
      options_(std::move(options)) {}

void IndexPool::Run(const std::function<bool()>& stopping,
                    const std::function<void(absl::Duration)>& sleep) {
  std::vector<std::thread> workers;
  workers.reserve(options_.slots);
  for (int slot = 0; slot < options_.slots; ++slot) {
    workers.emplace_back([this, &stopping, &sleep] { Work(stopping, sleep); });
  }
  for (std::thread& worker : workers) worker.join();
}

void IndexPool::Work(const std::function<bool()>& stopping,
                     const std::function<void(absl::Duration)>& sleep) {
  // Both live exactly as long as this thread, which is what makes the
  // connection its own.
  const std::unique_ptr<IndexQueue> queue = make_queue_();
  Poller poller(*queue, run_, poller_);

  while (!stopping()) {
    const absl::StatusOr<std::optional<Claim>> claim = poller.ClaimOne();
    if (!claim.ok()) {
      // A queue we cannot reach is a reason to wait and try again, not to
      // exit: the supervisor would only restart us into the same outage.
      LOG(ERROR) << "Claim failed: " << claim.status();
      sleep(options_.idle_wait);
      continue;
    }
    if (!claim->has_value()) {
      sleep(options_.idle_wait);
      continue;
    }

    const absl::Time started = absl::Now();
    const absl::StatusOr<RunOutcome> outcome = poller.RunClaimed(**claim);
    if (!outcome.ok()) {
      // The run is over either way; the row expires and somebody retries.
      LOG(ERROR) << "Could not write the outcome of " << (*claim)->job.id << ": "
                 << outcome.status();
      continue;
    }
    metrics_.RunFinished(*outcome, absl::Now() - started);
    LOG(INFO) << "Run finished: " << ToString(*outcome);
  }
}

}  // namespace one_d4_worker
