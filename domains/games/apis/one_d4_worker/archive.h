#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_ARCHIVE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_ARCHIVE_H

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/job.h"

namespace one_d4_worker {

/// One game as a monthly archive listed it. Every field is optional on the
/// wire, so every field here can be empty: one incomplete game must not
/// stop the month.
struct ArchivedGame {
  std::string url;
  std::string pgn;
  std::string time_class;
  std::string white_username;
  std::string black_username;
  int white_rating = 0;
  int black_rating = 0;
  /// chess.com's words, not notation. See result.h.
  std::string white_result;
  std::string black_result;
  /// chess.com's ECOUrl — a slug carrying the opening name, not a code.
  std::string eco_url;
  /// Seconds since the epoch, 0 when the archive did not say.
  int64_t end_time = 0;
};

/// Where a month's games come from.
///
/// A port so the run can be tested without chess.com: everything below it
/// is HTTP, and everything above it is the part with the bugs.
class ArchiveSource {
 public:
  virtual ~ArchiveSource() = default;

  /// The player's games for that month.
  ///
  /// A month the player was quiet in is an empty vector, not an error:
  /// chess.com serves it as 200 with an empty games list. NotFound means
  /// the archive is not there at all — a missing player, or an upstream
  /// failure on a listed archive — and fails the run like any other error
  /// (#1360), because completing a request on it would record "indexed, no
  /// games" for a month nobody read.
  virtual absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                               YearMonth month) = 0;

  /// The player's title ("GM", "WIM", ...), or "" for an untitled player.
  ///
  /// Same source, so same port. An error here never fails the run — a
  /// title is decoration on a row, and losing the month over it would be
  /// the tail wagging the dog — but it is not cached either, so a later
  /// month retries it.
  virtual absl::StatusOr<std::string> FetchTitle(std::string_view player) = 0;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_ARCHIVE_H
