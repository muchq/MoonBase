#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_CHESS_COM_ARCHIVE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_CHESS_COM_ARCHIVE_H

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/libs/chess_com_cpp/client.h"

namespace one_d4_worker {

/// The chess.com archive, as the run wants it.
///
/// Two jobs, both about the seam: smithy::Outcome becomes absl::Status, and
/// every optional field becomes a present-but-empty one. The run never sees
/// a nullopt, because "the archive did not say" and "the player has no
/// rating" are the same row either way.
class ChessComArchive : public ArchiveSource {
 public:
  explicit ChessComArchive(const chess_com::Client& client) : client_(client) {}

  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                       YearMonth month) override;

  absl::StatusOr<std::string> FetchTitle(std::string_view player) override;

 private:
  const chess_com::Client& client_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_CHESS_COM_ARCHIVE_H
