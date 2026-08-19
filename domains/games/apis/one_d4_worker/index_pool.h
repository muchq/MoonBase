#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H

#include <functional>
#include <memory>
#include <mutex>

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {

/// Indexes several requests at once.
///
/// Competing consumers: every thread claims a request and runs it. A
/// thread only claims when it is free to run what it claims, because it
/// is the same thread — so capacity is the thread count and there is no
/// accounting to get wrong. Claiming ahead would be worse than pointless:
/// a claim waiting for a thread is a lease being renewed for a range
/// nobody is indexing, which keeps every other worker off it.
///
/// Concurrent claims are safe at the database rather than here.
/// ClaimNext is one conditional UPDATE over FOR UPDATE SKIP LOCKED, so
/// two threads racing for a row cannot both win, and a claim this process
/// holds live is not a candidate for the next one.
class IndexPool {
 public:
  struct Options {
    /// How many requests may be in flight.
    ///
    /// Local capacity rather than a queue protocol constant — unlike the
    /// lease and the run ceiling, two workers may disagree about this
    /// without misbehaving — so it is a deployment knob.
    ///
    /// It is also what bounds concurrent requests against chess.com. A
    /// run fetches one month at a time and no longer looks up opponents
    /// one by one (#1403), so a run in flight is at most one request in
    /// flight. Should a run ever fan out internally again, that bound
    /// belongs on the calls rather than here.
    int slots = 4;

    /// How long to wait before asking an empty or unreachable queue again.
    absl::Duration idle_wait = absl::Seconds(5);
  };

  /// Makes the queue one thread claims through. Called once per thread,
  /// on that thread.
  ///
  /// Per thread rather than shared: one pg::Client is one connection
  /// serialised by a mutex, and a heartbeat blocked behind its own run's
  /// flush holds that mutex while it waits — stalling every other
  /// thread's claims and writes behind a lock that has nothing to do
  /// with them.
  using QueueFactory = std::function<std::unique_ptr<IndexQueue>()>;

  IndexPool(QueueFactory make_queue, Poller::Run run, Poller::Options poller,
            WorkerMetrics& metrics, Options options);

  /// Runs until `stopping`, then waits for the runs in flight to finish.
  ///
  /// Draining is the point of the wait: a run killed mid-month hands its
  /// claim back to nobody, so the range sits until the lease expires.
  ///
  /// `stopping` and `sleep` are called from every thread, so both have to
  /// be safe to call concurrently.
  void Run(const std::function<bool()>& stopping, const std::function<void(absl::Duration)>& sleep);

 private:
  void Work(const std::function<bool()>& stopping,
            const std::function<void(absl::Duration)>& sleep);

  /// Set by whichever thread notices the shutdown first, so the drain
  /// is announced when it starts rather than when it ends.
  std::once_flag announced_;
  absl::Notification draining_;

  const QueueFactory make_queue_;
  const Poller::Run run_;
  const Poller::Options poller_;
  WorkerMetrics& metrics_;
  const Options options_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H
