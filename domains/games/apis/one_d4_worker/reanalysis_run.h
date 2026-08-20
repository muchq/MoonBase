#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_RUN_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_RUN_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4_worker {

/// One stored game, as reanalysis reads it back.
struct StoredGame {
  std::string url;
  std::string pgn;
};

/// Where reanalysis reads from: the corpus already indexed.
class GameCorpus {
 public:
  virtual ~GameCorpus() = default;

  /// The next `limit` games with url greater than `after`, in url order.
  /// Empty `after` starts at the beginning; fewer than `limit` rows means
  /// the end.
  ///
  /// Keyset, not OFFSET. Paging by offset skips rows inserted mid-pass —
  /// the Java version's javadoc admitted it needed a second run to catch
  /// them — and a keyset walk in a stable order cannot.
  virtual absl::StatusOr<std::vector<StoredGame>> After(std::string_view after, int limit) = 0;
};

/// One game's fresh occurrences, replacing whatever it had.
struct ReanalyzedGame {
  std::string url;
  std::vector<one_d4::MotifOccurrence> occurrences;
};

/// Where reanalysis writes.
class OccurrenceSink {
 public:
  virtual ~OccurrenceSink() = default;

  /// Replaces every listed game's occurrences, atomically for the batch.
  ///
  /// Atomic because it has to be: a delete that commits separately from
  /// its insert leaves a window in which an indexing flush over one of
  /// these games inserts its own occurrences and both copies survive.
  /// ConcurrentFlushTest demonstrates that doubling on the Java side.
  virtual absl::Status Replace(const std::vector<ReanalyzedGame>& games) = 0;
};

/// What a pass reports back.
struct ReanalysisReport {
  int games_processed = 0;
  int games_failed = 0;
  /// The last url a finished page covered. Where a resume starts.
  std::string cursor;
  bool lease_lost = false;
  std::optional<Stopped> stopped;
};

/// A pass's handle to the claim it is working under.
///
/// Parallel to LeaseKeeper rather than reusing it: reanalysis reports a
/// position as well as a count, and a keeper that only carried the count
/// could not checkpoint a resume. #1417 covers what the two share.
class ReanalysisLease {
 public:
  virtual ~ReanalysisLease() = default;

  /// Whether the claim is still ours. False once it is lost.
  virtual bool Keep() = 0;

  /// Checkpoints position and counts together. Same answer as Keep(), from
  /// the same fence, so a refusal is a lost claim.
  virtual bool Report(std::string_view cursor, int games_processed, int games_failed) = 0;

  /// True once the pass has been going longer than any legitimate one.
  virtual bool OutOfTime() = 0;
};

/// Re-extracts every stored game, oldest url first.
///
/// Knows nothing about SQL — both ends are ports. What it owns is the
/// order: read a page, extract it, replace it, checkpoint, repeat.
class ReanalysisRun {
 public:
  struct Options {
    /// Matches IndexRun's, and reconciled to it deliberately (#1389): the
    /// write is the same delete+insert under the same FOR UPDATE locks, so
    /// the same page size bounds the same contention. It is also the
    /// checkpoint granularity — a bigger page is more work to redo when a
    /// pass is taken over.
    int batch_size = 100;

    /// True when the worker is shutting down. Checked between pages.
    std::function<bool()> stopping;
  };

  ReanalysisRun(GameCorpus& corpus, OccurrenceSink& sink, Options options);

  /// The corpus's or the sink's error when either fails. Otherwise a
  /// report, including for the passes that stopped early.
  absl::StatusOr<ReanalysisReport> Execute(const ReanalysisJob& job, ReanalysisLease& lease);

 private:
  GameCorpus& corpus_;
  OccurrenceSink& sink_;
  Options options_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_REANALYSIS_RUN_H
