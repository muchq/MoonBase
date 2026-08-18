#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
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
  std::string pgn;
  std::vector<one_d4::MotifOccurrence> occurrences;
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
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAME_SINK_H
