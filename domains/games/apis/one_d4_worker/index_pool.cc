#include "domains/games/apis/one_d4_worker/index_pool.h"

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
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
  // Waited for rather than logged straight away: this thread reaches
  // here the moment the workers are spawned, and the workers run until
  // the shutdown. Announced when that lands and not when the drain
  // finishes, because a run cannot interrupt a chess.com call it is
  // already inside — so the wait can take minutes, and silence there
  // looks exactly like a hang.
  draining_.WaitForNotification();
  LOG(INFO) << "Draining " << options_.slots << " indexing threads";

  for (std::thread& worker : workers) worker.join();
}

void IndexPool::Work(const std::function<bool()>& stopping,
                     const std::function<void(absl::Duration)>& sleep) {
  // Both live exactly as long as this thread, which is what makes the
  // connection its own.
  const std::unique_ptr<IndexQueue> queue = make_queue_();
  Poller poller(*queue, run_, poller_);
  const absl::Cleanup announce = [this] {
    std::call_once(announced_, [this] { draining_.Notify(); });
  };

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

    const IndexJob& job = (*claim)->job;
    LOG(INFO) << "Claimed request_id=" << job.id << " player=" << job.player
              << " months=" << job.start_month << ".." << job.end_month
              << " owner=" << (*claim)->owner;

    const absl::Time started = absl::Now();
    const absl::StatusOr<RunOutcome> outcome = poller.RunClaimed(**claim);
    if (!outcome.ok()) {
      // The run is over either way; the row expires and somebody retries.
      LOG(ERROR) << "Outcome not written request_id=" << job.id << " error=" << outcome.status();
      continue;
    }
    const absl::Duration elapsed = absl::Now() - started;
    metrics_.RunFinished(*outcome, elapsed);
    LOG(INFO) << "Finished request_id=" << job.id << " outcome=" << ToString(*outcome)
              << " duration_ms=" << absl::ToInt64Milliseconds(elapsed);
  }
}

}  // namespace one_d4_worker
