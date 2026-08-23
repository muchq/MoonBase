#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_QUEUE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_QUEUE_H

#include <optional>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/claim_ref.h"
#include "domains/games/apis/one_d4_worker/job.h"

namespace one_d4_worker {

/// The `indexing_requests` table, as a worker sees it.
///
/// Every write past ClaimNext is fenced on ownership and answers false when
/// the row is no longer ours. That is what lets any number of workers —
/// including ones that are not this process, or this language — poll the
/// same table (#1279).
class IndexQueue {
 public:
  virtual ~IndexQueue() = default;

  /// Claims the oldest request nobody holds, for `lease`. nullopt when
  /// there is nothing to claim.
  virtual absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                            absl::Duration lease) = 0;

  /// Extends our lease. False when we no longer hold it.
  virtual absl::StatusOr<bool> Heartbeat(ClaimRef claim, absl::Duration lease) = 0;

  /// Records how far a run has got, without finishing it. False when we no
  /// longer hold the claim.
  ///
  /// Non-terminal but fenced on the same terms as the writes below, so a
  /// run that has lost the range cannot walk the counter of a row somebody
  /// else is now working.
  virtual absl::StatusOr<bool> Progress(ClaimRef claim, int games_indexed) = 0;

  virtual absl::StatusOr<bool> Complete(ClaimRef claim, int games_indexed) = 0;

  virtual absl::StatusOr<bool> Fail(ClaimRef claim, std::string_view message) = 0;

  /// Gives the row back unfinished and refunds the attempt. For a shutdown,
  /// which is not the request's fault.
  virtual absl::StatusOr<bool> HandBack(ClaimRef claim) = 0;

  /// Gives the row back unfinished, attempt spent. For a run that hit its
  /// own ceiling — refunding that one would retry a slow range forever.
  virtual absl::StatusOr<bool> Release(ClaimRef claim) = 0;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_QUEUE_H
