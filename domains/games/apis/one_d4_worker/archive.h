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
  /// NotFound means the archive is not there, which for a month the player
  /// did not play is the ordinary answer — the run treats it as empty. Any
  /// other error fails the run.
  virtual absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                               YearMonth month) = 0;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_ARCHIVE_H
