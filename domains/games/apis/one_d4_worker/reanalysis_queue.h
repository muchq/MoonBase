#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_QUEUE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_QUEUE_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// One claimed row of `reanalysis_requests`.
struct ReanalysisJob {
  std::string id;

  /// The last game_url a finished page covered — an exclusive lower bound
  /// for the next. Empty on a pass that has not finished one yet.
  ///
  /// This is what the OFFSET paging it replaces could not do: a pass that
  /// loses its lease halfway through resumes where it stopped instead of
  /// starting the corpus again.
  std::string cursor_game_url;

  int games_processed = 0;
  int games_failed = 0;
  int attempts = 0;
};

/// The `reanalysis_requests` table, as a worker sees it.
///
/// Same claim/lease/fence protocol as IndexQueue and deliberately a
/// different table: the indexers claim from `indexing_requests` with no
/// job-type predicate, so a reanalysis row there is one they take, cannot
/// run, and fail. #1417 covers sharing the protocol itself, which wants a
/// conformance test before it wants a shared statement builder.
class ReanalysisQueue {
 public:
  virtual ~ReanalysisQueue() = default;

  virtual absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view owner,
                                                                 absl::Duration lease) = 0;

  virtual absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                         absl::Duration lease) = 0;

  /// Records how far the pass has got. Carries the cursor as well as the
  /// counts, because resuming needs the position and reporting needs the
  /// numbers, and writing them separately lets a crash between the two
  /// leave a count that does not match the position.
  virtual absl::StatusOr<bool> Progress(std::string_view id, std::string_view owner,
                                        std::string_view cursor_game_url, int games_processed,
                                        int games_failed) = 0;

  virtual absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner,
                                        int games_processed, int games_failed) = 0;

  virtual absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                                    std::string_view message) = 0;

  /// Gives the row back unfinished and refunds the attempt. For a shutdown,
  /// which is not the request's fault.
  virtual absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) = 0;

  /// Gives the row back unfinished, attempt spent.
  virtual absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) = 0;
};

/// ReanalysisQueue over Postgres.
class PgReanalysisQueue : public ReanalysisQueue {
 public:
  /// After this many attempts a pass stops being claimable. Same budget as
  /// PgQueue::kMaxAttempts, for the same reason: a job that kills its worker
  /// three times will kill the fourth.
  static constexpr int kMaxAttempts = 3;

  explicit PgReanalysisQueue(pg::Client& client) : client_(client) {}

  absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view owner,
                                                         absl::Duration lease) override;
  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override;
  absl::StatusOr<bool> Progress(std::string_view id, std::string_view owner,
                                std::string_view cursor_game_url, int games_processed,
                                int games_failed) override;
  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner, int games_processed,
                                int games_failed) override;
  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override;
  absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) override;
  absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) override;

 private:
  pg::Client& client_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_QUEUE_H
