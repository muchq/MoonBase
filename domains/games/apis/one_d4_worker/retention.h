#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_H

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/retention_policy.h"
#include "domains/games/apis/one_d4_worker/sweep_report.h"
#include "domains/platform/libs/pg/pg.h"

// The hourly sweep: delete what has aged out, settle what nobody is working
// on (#1424).
//
// It belongs in this process rather than in the Java service because its
// guarantees are cross-worker facts — "no worker anywhere holds a live lease"
// — and this is the process that holds the leases.
//
// The windows come from RetentionPolicy, which the worker loads at startup;
// see retention_policy.h.

namespace one_d4_worker {

/// Settles abandoned requests, then deletes what has aged out.
///
/// Settling comes first because releasing a lapsed claim returns work to the
/// queue, and a request deleted before that would take the work with it. The
/// deletes then run games → periods → requests, so a request and the games it
/// produced clear in one pass rather than over two: game_features.request_id
/// is a foreign key onto indexing_requests(id), and the request delete skips
/// any row a game still points at.
/// `report` is filled as each phase commits, and is meaningful even when this
/// returns an error: settling commits before the deletes are attempted, so a
/// failing delete leaves real settle counts behind. Reporting an empty one
/// would under-count exactly the pass where reclaiming mattered most.
absl::Status Sweep(pg::Client& client, const RetentionPolicy& policy, absl::Time now,
                   SweepReport& report);

/// The sentence a poisoned request carries, which embeds the attempt budget.
/// Matches the Java service's, which stored rows already have — both build it
/// from `max_attempts` in retention_policy.json, so neither can quote a number
/// the other does not enforce.
std::string PoisonedMessage(int max_attempts);

/// The sentence a stalled request carries, likewise.
extern const char kStalledMessage[];

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_H
