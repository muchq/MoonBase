#include "domains/games/apis/one_d4_worker/retention.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"

namespace one_d4_worker {
namespace {

/// Bound as a naive TIMESTAMP in UTC, which is what these columns hold and
/// what the Java service writes into them. Deliberately not NOW(): the sweep
/// takes the instant it is settling as of, so a test can advance past a window
/// instead of backdating rows — backdating only ever exercises the delete,
/// since rows at the epoch pass any positive window.
std::string Stamp(absl::Time t) {
  return absl::FormatTime("%Y-%m-%d %H:%M:%E6S", t, absl::UTCTimeZone());
}

/// The unheld test, shared by both retiring arms: no owner, or an owner whose
/// lease has lapsed.
constexpr char kUnheld[] =
    "(owner_id IS NULL OR lease_expires_at IS NULL OR lease_expires_at <= $1::timestamp)";

}  // namespace

const char kPoisonedMessage[] =
    "Abandoned: this request was attempted 3 times and each worker stopped before finishing it. "
    "Something about this range fails repeatedly rather than transiently.";

const char kStalledMessage[] =
    "Abandoned: no indexing worker has picked this up, and none is running anywhere. Re-submit "
    "once indexing is available again.";

absl::Status Sweep(pg::Client& client, const RetentionPolicy& policy, absl::Time now,
                   SweepReport& report) {
  const std::string at = Stamp(now);
  const std::string stale_cutoff = Stamp(now - policy.stale_request);
  const std::string attempts = std::to_string(PgQueue::kMaxAttempts);

  // Settling is one transaction; the deletes are another, and the boundary is
  // load bearing. The three arms have to agree — a released row that the
  // poisoned arm did not get to retire is a row handed back to the queue that
  // should have been failed — so they commit or roll back together.
  //
  // The deletes must not be able to undo them. A delete that trips the
  // statement timeout or waits on a lock would otherwise roll back the settles
  // with it, and a poisoned row that fails to reach FAILED is worse than
  // unsettled: it keeps attempts >= kMaxAttempts, which ClaimNext excludes, so
  // no worker will take it and the user is never told. It would sit invisible
  // until some later sweep got all the way through.
  //
  // The deletes are idempotent, so their own rollback costs an hour and
  // nothing else.
  const absl::Status settled = client.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    // Bounds every statement below. LOCAL, so it reverts with the transaction
    // rather than leaking onto a pooled connection.
    auto bound = tx.Exec(absl::StrCat("SET LOCAL statement_timeout = ",
                                      absl::ToInt64Milliseconds(policy.statement_timeout)));
    if (!bound.ok()) return bound.status();

    // The three arms run poisoned, stalled, released, and the order is load
    // bearing twice over.
    //
    // Poisoned before stalled: a row whose attempts are spent is also unheld
    // and may well be old, so it matches the stalled arm too. Both retire it;
    // only one tells the user why.
    //
    // Stalled before released: releasing stamps updated_at, which would make
    // the row look freshly touched and hide it from the staleness the stalled
    // arm looks for — costing the user another whole window of silence.
    auto poisoned = tx.Exec(absl::StrCat(R"(
        UPDATE indexing_requests
           SET status = 'FAILED', error_message = $2, dedupe_key = NULL,
               updated_at = $1::timestamp, owner_id = NULL, lease_expires_at = NULL
         WHERE status IN ('PENDING', 'PROCESSING')
           AND attempts >= $3::int
           AND )",
                                         kUnheld, " RETURNING id"),
                            {at, kPoisonedMessage, attempts});
    if (!poisoned.ok()) return poisoned.status();
    report.poisoned = poisoned->rows();

    // The NOT EXISTS pair is what separates a backlog from an outage. Age
    // alone cannot: one worker draining a deep queue leaves rows at the back
    // untouched for as long as the backlog takes. So this fires only when no
    // worker anywhere holds a live lease, and none has held one recently
    // enough to still be working.
    auto stalled = tx.Exec(absl::StrCat(R"(
        UPDATE indexing_requests
           SET status = 'FAILED', error_message = $2, dedupe_key = NULL,
               updated_at = $1::timestamp, owner_id = NULL, lease_expires_at = NULL
         WHERE status IN ('PENDING', 'PROCESSING')
           AND )",
                                        kUnheld, R"(
           AND updated_at < $3::timestamp
           AND NOT EXISTS (
                 SELECT 1 FROM indexing_requests live
                  WHERE live.owner_id IS NOT NULL
                    AND live.lease_expires_at > $1::timestamp)
           AND NOT EXISTS (
                 SELECT 1 FROM indexing_requests recent
                  WHERE recent.id <> indexing_requests.id
                    AND recent.lease_expires_at >= $3::timestamp)
        RETURNING id)"),
                           {at, kStalledMessage, stale_cutoff});
    if (!stalled.ok()) return stalled.status();
    report.stalled = stalled->rows();

    // Not a retirement: the owner is gone, the work is not. Another worker
    // claims it next, and the user is told nothing about work about to run.
    // Carries its own attempts guard, so a row at the limit cannot match here
    // at all — the ordering above is not what keeps them apart.
    auto released = tx.Exec(absl::StrCat(R"(
        UPDATE indexing_requests
           SET owner_id = NULL, updated_at = $1::timestamp
         WHERE status IN ('PENDING', 'PROCESSING')
           AND owner_id IS NOT NULL
           AND lease_expires_at IS NOT NULL
           AND lease_expires_at <= $1::timestamp
           AND attempts < $2::int
        RETURNING id)"),
                            {at, attempts});
    if (!released.ok()) return released.status();
    report.released = released->rows();

    return absl::OkStatus();
  });
  if (!settled.ok()) return settled;

  // Committing here also drops the row locks the release UPDATE took. Holding
  // them across the deletes would hide those rows from every other worker for
  // the length of the delete phase — ClaimNext takes its candidate with FOR
  // UPDATE SKIP LOCKED — which is the opposite of returning the work promptly.
  const absl::Status deleted = client.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    auto bound = tx.Exec(absl::StrCat("SET LOCAL statement_timeout = ",
                                      absl::ToInt64Milliseconds(policy.statement_timeout)));
    if (!bound.ok()) return bound.status();

    // Games before requests: game_features.request_id is a foreign key onto
    // indexing_requests(id), and the request delete skips any row a game still
    // points at. Reversing these would not corrupt anything — it would leave a
    // qualifying request for the next pass, once its games had gone.
    // motif_occurrences goes with the games, by ON DELETE CASCADE.
    const std::string games_cutoff = Stamp(now - policy.period);
    auto games =
        tx.Exec("DELETE FROM game_features WHERE indexed_at < $1::timestamp RETURNING game_url",
                {games_cutoff});
    if (!games.ok()) return games.status();
    report.games_deleted = games->rows();

    auto periods =
        tx.Exec("DELETE FROM indexed_periods WHERE fetched_at < $1::timestamp RETURNING id",
                {games_cutoff});
    if (!periods.ok()) return periods.status();
    report.periods_deleted = periods->rows();

    auto requests = tx.Exec(R"(
        DELETE FROM indexing_requests
         WHERE created_at < $1::timestamp
           AND status NOT IN ('PENDING', 'PROCESSING')
           AND NOT EXISTS (
                 SELECT 1 FROM game_features g WHERE g.request_id = indexing_requests.id)
        RETURNING id)",
                            {Stamp(now - policy.request)});
    if (!requests.ok()) return requests.status();
    report.requests_deleted = requests->rows();

    return absl::OkStatus();
  });
  return deleted;
}

}  // namespace one_d4_worker
