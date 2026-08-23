#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace one_d4_worker {
namespace {

// Every statement here ends in RETURNING id, for the reason pg_queue.cc
// gives: rows() is PQntuples, so without it a write that landed and a write
// that matched no row are indistinguishable.

std::string Seconds(absl::Duration lease) {
  return absl::StrCat(absl::ToInt64Seconds(lease), " seconds");
}

int ToInt(const std::optional<std::string>& value) {
  // SimpleAtoi leaves parsed unspecified on failure, so the 0 has to be
  // returned explicitly rather than read back out of parsed.
  int parsed = 0;
  if (!value.has_value() || !absl::SimpleAtoi(*value, &parsed)) return 0;
  return parsed;
}

}  // namespace

absl::StatusOr<std::optional<ReanalysisJob>> PgReanalysisQueue::ClaimNext(std::string_view owner,
                                                                          absl::Duration lease) {
  // Retire what the budget has exhausted before looking for work. Merely
  // not claiming it is not enough: a row left PROCESSING holds the
  // single-live slot forever — never claimable, never history, and every
  // later enqueue refused by idx_reanalysis_requests_single_live. The
  // indexing table has the Java retention worker for this; nothing else
  // tends this one.
  const auto retired = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET status = 'FAILED', owner_id = NULL, updated_at = NOW(),
             error_message = 'Reanalysis retired: attempt budget exhausted'
         WHERE status IN ('PENDING', 'PROCESSING')
           AND attempts >= $1
           AND (owner_id IS NULL
                OR lease_expires_at IS NULL OR lease_expires_at <= NOW())
         RETURNING id)",
      {std::to_string(kMaxAttempts)});
  if (!retired.ok()) return retired.status();

  // One conditional UPDATE, so two workers racing for the same row cannot
  // both win. FOR UPDATE SKIP LOCKED picks the candidate without the two of
  // them queueing behind each other first.
  //
  // Re-presenting the id already on the row does not spend an attempt;
  // presenting any other id does — a takeover is a real attempt, a resume
  // by the same run is not.
  const auto claimed = client_.Exec(
      R"(UPDATE reanalysis_requests SET
             owner_id = $1::text,
             status = 'PROCESSING',
             lease_expires_at = NOW() + $2::interval,
             updated_at = NOW(),
             attempts = CASE WHEN owner_id = $1::text THEN attempts ELSE attempts + 1 END
         WHERE id = (
             SELECT id FROM reanalysis_requests
             WHERE status IN ('PENDING', 'PROCESSING')
               AND attempts < $3
               AND (owner_id IS NULL
                    OR lease_expires_at IS NULL OR lease_expires_at <= NOW())
             ORDER BY created_at ASC, id ASC
             FOR UPDATE SKIP LOCKED
             LIMIT 1)
         RETURNING id, cursor_game_url, games_processed, games_failed, attempts)",
      {std::string(owner), Seconds(lease), std::to_string(kMaxAttempts)});
  if (!claimed.ok()) return claimed.status();
  if (claimed->rows() == 0) return std::nullopt;

  ReanalysisJob job;
  job.id = claimed->Get(0, 0).value_or("");
  // NULL and "" mean the same thing here — no page has finished — and the
  // run compares the cursor for emptiness, so collapsing them is the whole
  // handling this needs.
  job.cursor_game_url = claimed->Get(0, 1).value_or("");
  job.games_processed = ToInt(claimed->Get(0, 2));
  job.games_failed = ToInt(claimed->Get(0, 3));
  job.attempts = ToInt(claimed->Get(0, 4));
  return job;
}

absl::StatusOr<bool> PgReanalysisQueue::Heartbeat(std::string_view id, std::string_view owner,
                                                  absl::Duration lease) {
  // status IN, not status = 'PROCESSING'. #1417 found the two workers
  // fencing this step differently; this is the side that matches every
  // other fence in both languages.
  const auto held = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET lease_expires_at = NOW() + $3::interval, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2 AND status IN ('PENDING', 'PROCESSING')
         RETURNING id)",
      {std::string(id), std::string(owner), Seconds(lease)});
  if (!held.ok()) return held.status();
  return held->rows() > 0;
}

// Terminal and progress writes need a live lease, not just our name on the
// row: at expiry a takeover is already licensed even before a rival has
// written its own owner_id, and the old owner must not win that race.
absl::StatusOr<bool> PgReanalysisQueue::Progress(std::string_view id, std::string_view owner,
                                                 std::string_view cursor_game_url,
                                                 int games_processed, int games_failed) {
  // Cursor and counts in one statement. Written separately, a crash between
  // them leaves a pass that resumes from a position its numbers disagree
  // with, and the numbers are what the endpoint reports.
  const auto written = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET cursor_game_url = $3, games_processed = $4, games_failed = $5, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2
           AND status IN ('PENDING', 'PROCESSING')
           AND lease_expires_at > NOW()
         RETURNING id)",
      {std::string(id), std::string(owner), std::string(cursor_game_url),
       std::to_string(games_processed), std::to_string(games_failed)});
  if (!written.ok()) return written.status();
  return written->rows() > 0;
}

absl::StatusOr<bool> PgReanalysisQueue::Complete(std::string_view id, std::string_view owner,
                                                 int games_processed, int games_failed) {
  const auto written = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET status = 'COMPLETED', games_processed = $3, games_failed = $4,
             owner_id = NULL, error_message = NULL, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2
           AND status IN ('PENDING', 'PROCESSING')
           AND lease_expires_at > NOW()
         RETURNING id)",
      {std::string(id), std::string(owner), std::to_string(games_processed),
       std::to_string(games_failed)});
  if (!written.ok()) return written.status();
  return written->rows() > 0;
}

absl::StatusOr<bool> PgReanalysisQueue::Fail(std::string_view id, std::string_view owner,
                                             std::string_view message) {
  const auto written = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET status = 'FAILED', error_message = $3, owner_id = NULL, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2
           AND status IN ('PENDING', 'PROCESSING')
           AND lease_expires_at > NOW()
         RETURNING id)",
      {std::string(id), std::string(owner), std::string(message)});
  if (!written.ok()) return written.status();
  return written->rows() > 0;
}

// The cursor survives both of these on purpose: whoever picks the row up
// next resumes from where this run stopped.
absl::StatusOr<bool> PgReanalysisQueue::HandBack(std::string_view id, std::string_view owner) {
  const auto handed = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET owner_id = NULL, updated_at = NOW(),
             attempts = CASE WHEN attempts > 0 THEN attempts - 1 ELSE 0 END
         WHERE id = $1 AND owner_id = $2 AND status IN ('PENDING', 'PROCESSING')
         RETURNING id)",
      {std::string(id), std::string(owner)});
  if (!handed.ok()) return handed.status();
  return handed->rows() > 0;
}

absl::StatusOr<bool> PgReanalysisQueue::Release(std::string_view id, std::string_view owner) {
  const auto released = client_.Exec(
      R"(UPDATE reanalysis_requests
         SET owner_id = NULL, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2 AND status IN ('PENDING', 'PROCESSING')
         RETURNING id)",
      {std::string(id), std::string(owner)});
  if (!released.ok()) return released.status();
  return released->rows() > 0;
}

namespace {

/// PgReanalysisQueue plus the connection it claims over.
class OwnedReanalysisQueue : public ReanalysisQueue {
 public:
  explicit OwnedReanalysisQueue(const std::string& db_url) : client_(db_url), queue_(client_) {}

  absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view owner,
                                                         absl::Duration lease) override {
    return queue_.ClaimNext(owner, lease);
  }
  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override {
    return queue_.Heartbeat(id, owner, lease);
  }
  absl::StatusOr<bool> Progress(std::string_view id, std::string_view owner,
                                std::string_view cursor, int processed, int failed) override {
    return queue_.Progress(id, owner, cursor, processed, failed);
  }
  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner, int processed,
                                int failed) override {
    return queue_.Complete(id, owner, processed, failed);
  }
  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override {
    return queue_.Fail(id, owner, message);
  }
  absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) override {
    return queue_.HandBack(id, owner);
  }
  absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) override {
    return queue_.Release(id, owner);
  }

 private:
  pg::Client client_;
  PgReanalysisQueue queue_;
};

}  // namespace

std::unique_ptr<ReanalysisQueue> NewOwnedReanalysisQueue(const std::string& db_url) {
  return std::make_unique<OwnedReanalysisQueue>(db_url);
}

}  // namespace one_d4_worker
