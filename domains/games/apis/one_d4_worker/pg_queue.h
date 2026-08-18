#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H

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
  /// After this many attempts a request stops being claimable. Mirrors
  /// IndexingRequestStore.MAX_ATTEMPTS; PgQueueSchemaTest pins the pair.
  static constexpr int kMaxAttempts = 3;

  explicit PgQueue(pg::Client& client) : client_(client) {}

  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                    absl::Duration lease) override;
  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override;
  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner,
                                int games_indexed) override;
  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override;

 private:
  pg::Client& client_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_QUEUE_H
