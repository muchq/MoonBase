#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H

#include <memory>
#include <optional>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/queue.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// IndexQueue over the `indexing_requests` table the Java worker shares.
class PgQueue : public IndexQueue {
 public:
  /// `max_attempts` from retention_policy.json, which the Java service reads
  /// as IndexingRequestStore.MAX_ATTEMPTS. Passed in rather than held as a
  /// constant so the two cannot hold different numbers.
  PgQueue(pg::Client& client, int max_attempts) : client_(client), max_attempts_(max_attempts) {}

  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                    absl::Duration lease) override;
  absl::StatusOr<bool> Heartbeat(ClaimRef claim, absl::Duration lease) override;
  absl::StatusOr<bool> Progress(ClaimRef claim, int games_indexed) override;

  absl::StatusOr<bool> Complete(ClaimRef claim, int games_indexed) override;
  absl::StatusOr<bool> Fail(ClaimRef claim, std::string_view message) override;
  absl::StatusOr<bool> HandBack(ClaimRef claim) override;
  absl::StatusOr<bool> Release(ClaimRef claim) override;

 private:
  pg::Client& client_;
  int max_attempts_;
};

/// A queue with a connection of its own, for one indexing thread.
///
/// One pg::Client is one connection serialised by a mutex. Shared, a
/// heartbeat blocked behind that thread's own flush — the flush holds a
/// FOR UPDATE on the same row the heartbeat updates — holds the mutex
/// while it waits, and stalls every other thread's claims, heartbeats and
/// terminal writes behind it.
std::unique_ptr<IndexQueue> NewOwnedPgQueue(const std::string& db_url, int max_attempts);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H
