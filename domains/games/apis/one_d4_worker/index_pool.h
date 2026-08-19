#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H

#include <deque>
#include <functional>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/games/apis/one_d4_worker/poller.h"

namespace one_d4_worker {

/// Indexes several requests at once.
///
/// The shape a queue consumer usually takes: one thread claims, a fixed
/// set of threads run, and a claim is only made while a slot is free to
/// take it. Claiming ahead would be worse than pointless — a claim
/// waiting for a thread is a lease being renewed for a range nobody is
/// indexing, which keeps every other worker off it.
class IndexPool {
 public:
  struct Options {
    /// How many requests may be in flight.
    ///
    /// Local capacity rather than a queue protocol constant — unlike the
    /// lease and the run ceiling, two workers may disagree about this
    /// without misbehaving — so it is a deployment knob.
    int slots = 4;

    /// How long to wait before asking an empty or unreachable queue again.
    absl::Duration idle_wait = absl::Seconds(5);
  };

  IndexPool(Poller& poller, WorkerMetrics& metrics, Options options);

  /// Claims and dispatches until `stopping`, then waits for the runs in
  /// flight to finish.
  ///
  /// Draining is the point of the wait: a run killed mid-month hands its
  /// claim back to nobody, so the range sits until the lease expires.
  void Run(const std::function<bool()>& stopping, const std::function<void(absl::Duration)>& sleep);

 private:
  void Work();
  bool TakeSlot(const std::function<bool()>& stopping);
  void GiveBackSlot();
  void Dispatch(Claim claim);
  void Close();

  bool HasFreeSlot() const ABSL_SHARED_LOCKS_REQUIRED(mu_) { return busy_ < options_.slots; }
  bool HasWorkOrClosed() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
    return !pending_.empty() || closed_;
  }

  Poller& poller_;
  WorkerMetrics& metrics_;
  const Options options_;

  mutable absl::Mutex mu_;
  /// Claimed and not yet finished, which is what a slot counts. A claim
  /// waiting for a thread is as much in flight as one being indexed.
  int busy_ ABSL_GUARDED_BY(mu_) = 0;
  std::deque<Claim> pending_ ABSL_GUARDED_BY(mu_);
  bool closed_ ABSL_GUARDED_BY(mu_) = false;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_POOL_H
