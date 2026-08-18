#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4_worker {

/// One extracted game: a `game_features` row and the `motif_occurrences`
/// rows that hang off it.
struct IndexedGame {
  std::string url;
  std::string platform;
  std::string white_username;
  std::string black_username;
  int white_elo = 0;
  int black_elo = 0;
  std::string white_title;
  std::string black_title;
  std::string time_class;
  /// From the PGN [ECO] tag.
  std::string eco;
  /// From the archive's ECOUrl. See openings.h.
  std::string opening_name;
  std::string opening_family;
  /// Notation: "1-0", "0-1", "1/2-1/2", "unknown".
  std::string result;
  int64_t played_at = 0;
  int num_moves = 0;
  /// Seconds since the epoch, from the worker's clock rather than the
  /// database's. Retention compares this column against a threshold the
  /// worker supplies, so a row stamped by the server straddles two clocks
  /// and drifts with host skew (#1268).
  int64_t indexed_at = 0;
  std::string pgn;
  std::vector<one_d4::MotifOccurrence> occurrences;
};

/// One month, as `indexed_periods` records it: the cache that answers
/// "has this month been read" without going back to chess.com.
struct IndexedMonth {
  std::string player;
  std::string platform;
  /// "YYYY-MM".
  std::string month;
  /// Seconds since the epoch, taken before the month's games were written.
  ///
  /// Stamped up front so the period is never newer than the games it
  /// vouches for. Retention compares both against one threshold, so a
  /// period stamped afterwards would outlive its games and keep claiming
  /// they are there.
  int64_t fetched_at = 0;
  int games = 0;
  /// False when something the row should have carried could not be read,
  /// so a later request refetches the month.
  bool complete = true;
  bool exclude_bullet = false;
};

/// Where extracted games go.
///
/// A batch is one unit: a sink either writes all of it or none of it, so a
/// run that fails mid-month leaves no game with half its motifs.
class GameSink {
 public:
  virtual ~GameSink() = default;

  /// FailedPrecondition means the fence refused — the caller no longer
  /// owns the request, and nothing was written. It is a stopping
  /// condition, not a failure: whoever holds the range now owns its
  /// outcome. Every other error is a failure to write.
  ///
  /// The fence belongs here rather than in a check before the call. A
  /// separate check is a snapshot: the takeover can commit in the gap and
  /// the batch lands against a request this worker has already lost.
  virtual absl::Status Write(absl::Span<const IndexedGame> games) = 0;

  /// The count on a complete period for this month, or nullopt when there
  /// is none — the month has not been indexed, or was indexed under the
  /// other bullet filter, or was stored incomplete for refetching.
  ///
  /// The read half of the row RecordMonth writes, which is why it lives
  /// here rather than behind a port of its own.
  virtual absl::StatusOr<std::optional<int>> MonthAlreadyIndexed(const IndexedMonth& month) = 0;

  /// Records that a month was read.
  ///
  /// Unfenced, because indexed_periods is keyed by (player, platform,
  /// month) and has no request to condition on. What keeps it safe is
  /// order: it runs after a write that checked ownership, so a run that
  /// lost the lease never reaches it.
  virtual absl::Status RecordMonth(const IndexedMonth& month) = 0;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H
