#include "domains/games/apis/one_d4_worker/index_pool.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <thread>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "domains/games/apis/one_d4_worker/queue.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace one_d4_worker {
namespace {

/// An endless supply of requests, or none, or an outage.
class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view /*owner*/,
                                                    absl::Duration /*lease*/) override {
    const absl::MutexLock lock(mu_);
    ++claims_;
    if (!status_.ok()) return status_;
    if (empty_) return std::nullopt;
    IndexJob job;
    job.id = absl::StrCat("job-", claims_);
    job.player = "hikaru";
    job.platform = "chess.com";
    job.start_month = "2026-01";
    job.end_month = "2026-01";
    return job;
  }

  absl::StatusOr<bool> Heartbeat(std::string_view, std::string_view, absl::Duration) override {
    return true;
  }
  absl::StatusOr<bool> Progress(std::string_view, std::string_view, int) override { return true; }
  absl::StatusOr<bool> Complete(std::string_view, std::string_view, int) override { return true; }
  absl::StatusOr<bool> Fail(std::string_view, std::string_view, std::string_view) override {
    return true;
  }
  absl::StatusOr<bool> HandBack(std::string_view, std::string_view) override { return true; }
  absl::StatusOr<bool> Release(std::string_view, std::string_view) override { return true; }

  int claims() const {
    const absl::MutexLock lock(mu_);
    return claims_;
  }
  void set_empty() {
    const absl::MutexLock lock(mu_);
    empty_ = true;
  }
  void set_status(absl::Status status) {
    const absl::MutexLock lock(mu_);
    status_ = std::move(status);
  }

 private:
  mutable absl::Mutex mu_;
  int claims_ ABSL_GUARDED_BY(mu_) = 0;
  bool empty_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status status_ ABSL_GUARDED_BY(mu_);
};

/// Runs that block until released, counting how many were in flight at
/// once. The only way to see a pool's capacity is to fill it.
class BlockingRuns {
 public:
  Poller::Run AsRun() {
    return [this](const Claim&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
      const absl::MutexLock lock(mu_);
      ++running_;
      ++started_;
      peak_ = std::max(peak_, running_);
      mu_.Await(absl::Condition(&released_));
      --running_;
      return RunReport{};
    };
  }

  /// Waits for `n` runs to have started. Fails rather than hangs.
  void AwaitStarted(int n) {
    const absl::MutexLock lock(mu_);
    const auto enough = [this, n] { return started_ >= n; };
    ASSERT_TRUE(mu_.AwaitWithTimeout(absl::Condition(&enough), absl::Seconds(10)))
        << "only " << started_ << " of " << n << " runs started";
  }

  void Release() {
    const absl::MutexLock lock(mu_);
    released_ = true;
  }
  int peak() const {
    const absl::MutexLock lock(mu_);
    return peak_;
  }
  int started() const {
    const absl::MutexLock lock(mu_);
    return started_;
  }

 private:
  mutable absl::Mutex mu_;
  bool released_ ABSL_GUARDED_BY(mu_) = false;
  int running_ ABSL_GUARDED_BY(mu_) = 0;
  int peak_ ABSL_GUARDED_BY(mu_) = 0;
  int started_ ABSL_GUARDED_BY(mu_) = 0;
};

Poller::Options PollerOptions() {
  Poller::Options options;
  options.owner = "cpp/test";
  options.lease = absl::Minutes(5);
  options.renew_every = absl::Minutes(5);
  return options;
}

IndexPool::Options PoolOptions(int slots) {
  IndexPool::Options options;
  options.slots = slots;
  options.idle_wait = absl::Milliseconds(1);
  return options;
}

TEST(IndexPool, RunsSeveralRequestsAtOnce) {
  // The whole point. One loop indexes one request at a time, and a run
  // is mostly waiting on chess.com and Postgres.
  FakeQueue queue;
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(3));

  std::atomic<bool> stopping{false};
  std::thread driver(
      [&] { pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {}); });
  runs.AwaitStarted(3);

  EXPECT_EQ(runs.peak(), 3);

  stopping = true;
  runs.Release();
  driver.join();
}

TEST(IndexPool, ClaimsNothingWhileEverySlotIsBusy) {
  // A claim waiting for a thread is a lease renewed for a range nobody is
  // indexing, and every other worker is kept off it meanwhile.
  FakeQueue queue;
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  std::atomic<bool> stopping{false};
  std::thread driver(
      [&] { pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {}); });
  runs.AwaitStarted(2);
  absl::SleepFor(absl::Milliseconds(200));

  EXPECT_EQ(queue.claims(), 2) << "it claimed a range it had nowhere to run";

  stopping = true;
  runs.Release();
  driver.join();
}

TEST(IndexPool, WaitsForTheRunsInFlightBeforeItReturns) {
  // A run killed mid-month hands its claim back to nobody, so the range
  // sits until the lease expires — five minutes of a request looking
  // like it is being worked on.
  FakeQueue queue;
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  std::atomic<bool> stopping{false};
  std::atomic<bool> returned{false};
  std::thread driver([&] {
    pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {});
    returned = true;
  });
  runs.AwaitStarted(2);

  stopping = true;
  absl::SleepFor(absl::Milliseconds(200));
  EXPECT_FALSE(returned.load()) << "it left two runs writing";

  runs.Release();
  driver.join();
  EXPECT_TRUE(returned.load());
}

TEST(IndexPool, KeepsClaimingAsSlotsFreeUp) {
  FakeQueue queue;
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  std::atomic<bool> stopping{false};
  std::thread driver(
      [&] { pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {}); });
  runs.AwaitStarted(2);
  runs.Release();
  runs.AwaitStarted(6);

  EXPECT_GE(queue.claims(), 6);

  stopping = true;
  driver.join();
}

TEST(IndexPool, CountsEveryRunItFinished) {
  FakeQueue queue;
  BlockingRuns runs;
  runs.Release();
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  std::atomic<bool> stopping{false};
  std::thread driver(
      [&] { pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {}); });
  runs.AwaitStarted(4);
  stopping = true;
  driver.join();

  EXPECT_GT(recorder.CounterTotal(kRunsMetric,
                                  {{"outcome", "completed"}, {kIndexerLabel, kIndexerValue}}),
            0);
}

TEST(IndexPool, WaitsBeforeAskingAnEmptyQueueAgain) {
  // Without it an empty queue is a spin: one round trip per iteration
  // per slot, for as long as there is nothing to do.
  FakeQueue queue;
  queue.set_empty();
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  absl::Mutex mu;
  int slept = 0;
  std::thread driver([&] {
    pool.Run(
        [&mu, &slept] {
          const absl::MutexLock lock(mu);
          return slept >= 3;
        },
        [&mu, &slept](absl::Duration) {
          const absl::MutexLock lock(mu);
          ++slept;
        });
  });
  driver.join();

  EXPECT_EQ(queue.claims(), 3) << "it asked without waiting in between";
}

TEST(IndexPool, KeepsGoingAfterAQueueItCannotReach) {
  // Exiting would only have the supervisor restart us into the same
  // outage.
  FakeQueue queue;
  queue.set_status(absl::UnavailableError("no route to the database"));
  BlockingRuns runs;
  Poller poller(queue, runs.AsRun(), PollerOptions());
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool(poller, metrics, PoolOptions(2));

  absl::Mutex mu;
  int slept = 0;
  std::thread driver([&] {
    pool.Run(
        [&mu, &slept] {
          const absl::MutexLock lock(mu);
          return slept >= 3;
        },
        [&mu, &slept](absl::Duration) {
          const absl::MutexLock lock(mu);
          ++slept;
        });
  });
  driver.join();

  EXPECT_EQ(queue.claims(), 3) << "it gave up after the first failure";
}

}  // namespace
}  // namespace one_d4_worker
