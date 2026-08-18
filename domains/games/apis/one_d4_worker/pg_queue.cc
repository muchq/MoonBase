#include "domains/games/apis/one_d4_worker/pg_queue.h"

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace one_d4_worker {
namespace {

// Every statement here ends in RETURNING id, and the fencing depends on it:
// pg::Result::rows() is PQntuples, which is zero for an UPDATE that returns
// nothing at all — so without it a write that landed and a write that
// matched no row are indistinguishable, and every fenced call answers
// "not mine".

std::string Seconds(absl::Duration lease) {
  return absl::StrCat(absl::ToInt64Seconds(lease), " seconds");
}

bool IsTrue(const std::optional<std::string>& value) {
  return value.has_value() && (*value == "t" || *value == "true");
}

int ToInt(const std::optional<std::string>& value) {
  int parsed = 0;
  if (value.has_value()) absl::SimpleAtoi(*value, &parsed);
  return parsed;
}

}  // namespace

absl::StatusOr<std::optional<IndexJob>> PgQueue::ClaimNext(std::string_view owner,
                                                           absl::Duration lease) {
  // One conditional UPDATE, so two workers racing for the same row cannot
  // both win: the row lock decides, and the loser's WHERE no longer
  // matches. FOR UPDATE SKIP LOCKED picks the candidate without the two of
  // them queueing behind each other first.
  //
  // Renewing our own claim does not spend an attempt; taking one from an
  // expired lease does.
  const auto claimed = client_.Exec(
      R"(UPDATE indexing_requests SET
             owner_id = $1,
             status = 'PROCESSING',
             lease_expires_at = NOW() + $2::interval,
             updated_at = NOW(),
             attempts = CASE WHEN owner_id = $1 THEN attempts ELSE attempts + 1 END
         WHERE id = (
             SELECT id FROM indexing_requests
             WHERE status IN ('PENDING', 'PROCESSING')
               AND attempts < $3
               AND (owner_id IS NULL OR owner_id = $1
                    OR lease_expires_at IS NULL OR lease_expires_at <= NOW())
             ORDER BY created_at ASC, id ASC
             FOR UPDATE SKIP LOCKED
             LIMIT 1)
         RETURNING id, player, platform, start_month, end_month, exclude_bullet, skip_cache,
                   attempts)",
      {std::string(owner), Seconds(lease), std::to_string(kMaxAttempts)});
  if (!claimed.ok()) return claimed.status();
  if (claimed->rows() == 0) return std::nullopt;

  IndexJob job;
  job.id = claimed->Get(0, 0).value_or("");
  job.player = claimed->Get(0, 1).value_or("");
  job.platform = claimed->Get(0, 2).value_or("");
  job.start_month = claimed->Get(0, 3).value_or("");
  job.end_month = claimed->Get(0, 4).value_or("");
  job.exclude_bullet = IsTrue(claimed->Get(0, 5));
  job.skip_cache = IsTrue(claimed->Get(0, 6));
  job.attempts = ToInt(claimed->Get(0, 7));
  return job;
}

absl::StatusOr<bool> PgQueue::Heartbeat(std::string_view id, std::string_view owner,
                                        absl::Duration lease) {
  const auto held = client_.Exec(
      R"(UPDATE indexing_requests
         SET lease_expires_at = NOW() + $3::interval, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2 AND status = 'PROCESSING'
         RETURNING id)",
      {std::string(id), std::string(owner), Seconds(lease)});
  if (!held.ok()) return held.status();
  return held->rows() > 0;
}

absl::StatusOr<bool> PgQueue::Complete(std::string_view id, std::string_view owner,
                                       int games_indexed) {
  const auto written = client_.Exec(
      R"(UPDATE indexing_requests
         SET status = 'COMPLETED', games_indexed = $3, owner_id = NULL, dedupe_key = NULL,
             error_message = NULL, updated_at = NOW()
         WHERE id = $1 AND owner_id = $2
         RETURNING id)",
      {std::string(id), std::string(owner), std::to_string(games_indexed)});
  if (!written.ok()) return written.status();
  return written->rows() > 0;
}

absl::StatusOr<bool> PgQueue::Fail(std::string_view id, std::string_view owner,
                                   std::string_view message) {
  const auto written = client_.Exec(
      R"(UPDATE indexing_requests
         SET status = 'FAILED', error_message = $3, owner_id = NULL, dedupe_key = NULL,
             updated_at = NOW()
         WHERE id = $1 AND owner_id = $2
         RETURNING id)",
      {std::string(id), std::string(owner), std::string(message)});
  if (!written.ok()) return written.status();
  return written->rows() > 0;
}

}  // namespace one_d4_worker
