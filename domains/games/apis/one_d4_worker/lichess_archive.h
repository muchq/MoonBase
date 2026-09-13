#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_LICHESS_ARCHIVE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_LICHESS_ARCHIVE_H

#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/libs/lichess_cpp/client.h"

namespace one_d4_worker {

/// The Lichess archive, as the run wants it.
///
/// Lichess has no monthly endpoint: it takes a half-open millisecond range,
/// and a month is one. What comes back is concatenated PGN rather than a
/// list of game objects, so everything the row needs is read out of the tag
/// pairs — which is also why the titles chess.com withholds are free here.
class LichessArchive : public ArchiveSource {
 public:
  explicit LichessArchive(const lichess::Client& client) : client_(client) {}

  /// One export at a time, which is a rule rather than a courtesy: Lichess
  /// answers concurrent exports with 429 "Please only run 1 request(s) at a
  /// time", and the cooldown has been observed to outlast a 75-second
  /// backoff. The worker runs ONE_D4_INDEX_SLOTS requests at once — four in
  /// the deployed compose — against this one archive, so without the lock
  /// four LICHESS claims would race straight into that refusal and spend
  /// their attempts discovering it. Backoff only reacts after the rule is
  /// broken; this keeps it unbroken.
  ///
  /// Per process. Two replicas can still overlap, which is a deployment
  /// question rather than one this class can answer.
  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                       YearMonth month) override;

 private:
  const lichess::Client& client_;
  absl::Mutex one_at_a_time_;
};

/// Splits concatenated PGN into one string per game, each holding the text
/// it arrived as.
///
/// Exposed for testing, and separate because the parser hands back tags and
/// moves rather than source: the row stores the PGN itself, so something has
/// to keep the bytes. Games are cut at a line beginning "[Event ", the one
/// tag every Lichess game opens with.
std::vector<std::string_view> SplitPgnGames(std::string_view pgn);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_LICHESS_ARCHIVE_H
