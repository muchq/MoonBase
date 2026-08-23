#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_SWEEP_REPORT_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_SWEEP_REPORT_H

namespace one_d4_worker {

/// What one sweep did. Counts are rows affected, for the metrics and the log.
///
/// Its own header, with no dependencies, because the metrics layer takes one
/// of these and nothing else about the sweep. Declared alongside Sweep it
/// would put a Postgres client and a JSON parser in the compile of every
/// target that records a counter.
struct SweepReport {
  int poisoned = 0;
  int stalled = 0;
  int released = 0;

  int games_deleted = 0;
  int periods_deleted = 0;
  int requests_deleted = 0;

  int settled() const { return poisoned + stalled + released; }
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_SWEEP_REPORT_H
