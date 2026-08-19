#include "domains/games/apis/one_d4_worker/index_pool.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "domains/games/apis/one_d4_worker/queue.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// An endless supply of requests, or none, or an outage.
class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                    absl::Duration /*lease*/) override {
    const absl::MutexLock lock(mu_);
    ++claims_;
    owners_.insert(std::string(owner));
    if (!status_.ok()) return status_;
    if (empty_ || claims_ > jobs_) return std::nullopt;
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
  absl::StatusOr<bool> Complete(std::string_view, std::string_view, int) override {
    const absl::MutexLock lock(mu_);
    if (!terminal_status_.ok()) return terminal_status_;
    return true;
  }
  absl::StatusOr<bool> Fail(std::string_view, std::string_view, std::string_view) override {
    return true;
  }
  absl::StatusOr<bool> HandBack(std::string_view, std::string_view) override { return true; }
  absl::StatusOr<bool> Release(std::string_view, std::string_view) override { return true; }

  int claims() const {
    const absl::MutexLock lock(mu_);
    return claims_;
  }
  /// How many distinct ids claimed. One per claim, or two runs share a
  /// fencing token.
  size_t distinct_owners() const {
    const absl::MutexLock lock(mu_);
    return owners_.size();
  }
  void set_empty() {
    const absl::MutexLock lock(mu_);
    empty_ = true;
  }
  void set_status(absl::Status status) {
    const absl::MutexLock lock(mu_);
    status_ = std::move(status);
  }
  /// Hand out at most `n` requests, then answer empty.
  void set_jobs(int n) {
    const absl::MutexLock lock(mu_);
    jobs_ = n;
  }
  /// A terminal write the queue will not accept at all — distinct from
  /// one it refuses, which is a lost lease.
  void set_terminal_status(absl::Status status) {
    const absl::MutexLock lock(mu_);
    terminal_status_ = std::move(status);
  }

 private:
  mutable absl::Mutex mu_;
  int claims_ ABSL_GUARDED_BY(mu_) = 0;
  bool empty_ ABSL_GUARDED_BY(mu_) = false;
  int jobs_ ABSL_GUARDED_BY(mu_) = std::numeric_limits<int>::max();
  absl::Status status_ ABSL_GUARDED_BY(mu_);
  absl::Status terminal_status_ ABSL_GUARDED_BY(mu_);
  std::set<std::string> owners_ ABSL_GUARDED_BY(mu_);
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
    want_ = n;
    ASSERT_TRUE(mu_.AwaitWithTimeout(absl::Condition(this, &BlockingRuns::EnoughStarted),
                                     absl::Seconds(10)))
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
  bool EnoughStarted() const ABSL_SHARED_LOCKS_REQUIRED(mu_) { return started_ >= want_; }

  mutable absl::Mutex mu_;
  int want_ ABSL_GUARDED_BY(mu_) = 0;
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

/// Forwards to one shared queue, so a test can count what every thread
/// did while the pool still gets a queue of its own per thread.
class SharedQueue : public IndexQueue {
 public:
  explicit SharedQueue(IndexQueue& to) : to_(to) {}

  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                    absl::Duration lease) override {
    return to_.ClaimNext(owner, lease);
  }
  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override {
    return to_.Heartbeat(id, owner, lease);
  }
  absl::StatusOr<bool> Progress(std::string_view id, std::string_view owner, int games) override {
    return to_.Progress(id, owner, games);
  }
  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner, int games) override {
    return to_.Complete(id, owner, games);
  }
  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override {
    return to_.Fail(id, owner, message);
  }
  absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) override {
    return to_.HandBack(id, owner);
  }
  absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) override {
    return to_.Release(id, owner);
  }

 private:
  IndexQueue& to_;
};

/// Ends the loop once the queue has been asked `claims` times, and after
/// two seconds whatever happens.
///
/// The deadline is the point: a change that stops the loop claiming has
/// to fail an assertion rather than spin until the test times out. That
/// distinction was built once already and lost when this replaced
/// PollLoop.
///
/// Tests that count exactly run one thread. Several threads all claim,
/// so the count overshoots by however many were mid-claim when the last
/// one tipped it over — real, and nothing to do with what those tests
/// are about.
std::function<bool()> StopAfter(const FakeQueue& queue, int claims) {
  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  return [&queue, claims, deadline] { return queue.claims() >= claims || absl::Now() > deadline; };
}

/// Records what the loop was asked to wait for, rather than waiting.
class Naps {
 public:
  std::function<void(absl::Duration)> AsSleep() {
    return [this](absl::Duration wait) {
      const absl::MutexLock lock(mu_);
      waits_.push_back(wait);
    };
  }
  std::vector<absl::Duration> waits() const {
    const absl::MutexLock lock(mu_);
    return waits_;
  }

 private:
  mutable absl::Mutex mu_;
  std::vector<absl::Duration> waits_ ABSL_GUARDED_BY(mu_);
};

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
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(3));

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
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(2));

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
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(2));

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
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(2));

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

TEST(IndexPool, WaitsBeforeAskingAnEmptyQueueAgain) {
  // Without it an empty queue is a spin: one round trip per iteration,
  // for as long as there is nothing to do.
  FakeQueue queue;
  queue.set_empty();
  BlockingRuns runs;
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  Naps naps;
  pool.Run(StopAfter(queue, 3), naps.AsSleep());

  EXPECT_EQ(queue.claims(), 3);
  EXPECT_THAT(naps.waits(),
              ElementsAre(absl::Milliseconds(1), absl::Milliseconds(1), absl::Milliseconds(1)))
      << "it asked again without waiting, or waited for the wrong thing";
}

TEST(IndexPool, KeepsGoingAfterAQueueItCannotReach) {
  // Exiting would only have the supervisor restart us into the same
  // outage.
  FakeQueue queue;
  queue.set_status(absl::UnavailableError("no route to the database"));
  BlockingRuns runs;
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  Naps naps;
  pool.Run(StopAfter(queue, 3), naps.AsSleep());

  EXPECT_EQ(queue.claims(), 3) << "it gave up after the first failure";
  EXPECT_EQ(naps.waits().size(), 3u);
}

TEST(IndexPool, DoesNotWaitAfterAClaimThatDidWork) {
  // A queue with a backlog should be drained, not sipped from every five
  // seconds.
  FakeQueue queue;
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  Naps naps;
  pool.Run(StopAfter(queue, 5), naps.AsSleep());

  EXPECT_EQ(queue.claims(), 5);
  EXPECT_THAT(naps.waits(), IsEmpty()) << "it waited between claims that were both there";
}

TEST(IndexPool, ClaimsNothingWhenItStartsShuttingDown) {
  // A SIGTERM during startup should not take a range out of the queue on
  // its way past.
  FakeQueue queue;
  BlockingRuns runs;
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  Naps naps;
  pool.Run([] { return true; }, naps.AsSleep());

  EXPECT_EQ(queue.claims(), 0);
  EXPECT_THAT(naps.waits(), IsEmpty());
}

TEST(IndexPool, KeepsClaimingWhenATerminalWriteWillNotLand) {
  // A run whose outcome could not be written is still a finished run.
  // Treated as fatal, a database that refuses one write stops the thread
  // for good — and nothing else in these tests makes RunClaimed fail, so
  // nothing else would notice.
  FakeQueue queue;
  queue.set_terminal_status(absl::UnavailableError("no route to the database"));
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  Naps naps;
  pool.Run(StopAfter(queue, 6), naps.AsSleep());

  EXPECT_EQ(queue.claims(), 6) << "it ran out of slots";
}

TEST(IndexPool, CountsEveryRunItFinished) {
  FakeQueue queue;
  queue.set_jobs(3);
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(2));

  Naps naps;
  pool.Run(StopAfter(queue, 6), naps.AsSleep());

  EXPECT_EQ(recorder.CounterTotal(kRunsMetric,
                                  {{"outcome", "completed"}, {kIndexerLabel, kIndexerValue}}),
            3);
}

TEST(IndexPool, GivesEveryThreadAQueueOfItsOwn) {
  // One pg::Client is one connection serialised by a mutex, so a shared
  // queue puts every thread's claims, heartbeats and terminal writes in
  // one line — behind, among other things, a heartbeat waiting on a row
  // lock another thread's flush is holding.
  FakeQueue queue;
  BlockingRuns runs;
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  std::atomic<int> queues{0};
  IndexPool pool(
      [&queue, &queues] {
        ++queues;
        return std::make_unique<SharedQueue>(queue);
      },
      runs.AsRun(), PollerOptions(), metrics, PoolOptions(3));

  std::atomic<bool> stopping{false};
  std::thread driver(
      [&] { pool.Run([&stopping] { return stopping.load(); }, [](absl::Duration) {}); });
  runs.AwaitStarted(3);

  EXPECT_EQ(queues.load(), 3);

  stopping = true;
  runs.Release();
  driver.join();
}

TEST(IndexPool, NoTwoRunsClaimUnderTheSameId) {
  // Every thread has a poller of its own now, so a counter would start
  // at the same place in each and two runs would present one token —
  // and two runs sharing a token both pass every fence, which is the
  // whole thing a per-run id exists to stop.
  FakeQueue queue;
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(4));

  pool.Run(StopAfter(queue, 40), [](absl::Duration) {});

  EXPECT_EQ(queue.distinct_owners(), static_cast<size_t>(queue.claims()));
}

// ---- What the log has to say with several threads interleaving ----

TEST(IndexPool, SaysWhatItClaimed) {
  // Four threads make "Run finished" on its own useless: four identical
  // lines and no way to tie any of them to a request, a player or a
  // duration. The claim line is what every later line hangs off.
  FakeQueue queue;
  queue.set_jobs(1);
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kInfo, testing::_,
                  testing::AllOf(testing::HasSubstr("job-1"), testing::HasSubstr("hikaru"),
                                 testing::HasSubstr("2026-01"), testing::HasSubstr("cpp/test/"))));
  log.StartCapturingLogs();
  pool.Run(StopAfter(queue, 2), [](absl::Duration) {});
  log.StopCapturingLogs();
}

TEST(IndexPool, SaysWhichRunFinishedAndHowLongItTook) {
  // Without the id the line cannot be matched to its claim, and without
  // the elapsed there is no way to see a run that is dragging.
  FakeQueue queue;
  queue.set_jobs(1);
  BlockingRuns runs;
  runs.Release();
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(1));

  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_,
                       testing::AllOf(testing::HasSubstr("job-1"), testing::HasSubstr("completed"),
                                      testing::HasSubstr("ms"))));
  log.StartCapturingLogs();
  pool.Run(StopAfter(queue, 2), [](absl::Duration) {});
  log.StopCapturingLogs();
}

TEST(IndexPool, SaysItIsDrainingBeforeItWaits) {
  // The drain can take as long as a chess.com call the run is already
  // inside, and the stop grace is four minutes. Silence there looks
  // exactly like a hang.
  FakeQueue queue;
  queue.set_empty();
  BlockingRuns runs;
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  IndexPool pool([&queue] { return std::make_unique<SharedQueue>(queue); }, runs.AsRun(),
                 PollerOptions(), metrics, PoolOptions(2));

  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_, testing::HasSubstr("Draining")));
  log.StartCapturingLogs();
  pool.Run([] { return true; }, [](absl::Duration) {});
  log.StopCapturingLogs();
}

}  // namespace
}  // namespace one_d4_worker
