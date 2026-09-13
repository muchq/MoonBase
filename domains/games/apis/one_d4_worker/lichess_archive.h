#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_LICHESS_ARCHIVE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_LICHESS_ARCHIVE_H

#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
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

  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                       YearMonth month) override;

 private:
  const lichess::Client& client_;
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
