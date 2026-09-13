#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// A batch's stated titles, by platform, deduped on (platform, username)
/// keeping the newest observation and ordered by that key.
///
/// Ordered because player_titles is a second table two flushes can contend
/// over, and sorting the games by url does not order rows in it: two runs
/// meeting the same titled players through different games, in opposite
/// order, would each hold a row the other wants. Deduped because Postgres
/// refuses an ON CONFLICT that would touch one row twice, which is what a
/// player titled in two games of the same batch would do.
std::map<std::string, std::vector<TitleObservation>> TitleObservationsOf(
    absl::Span<const IndexedGame* const> games);

/// Writes `game_features` and `motif_occurrences` for one request.
///
/// One transaction per batch, fenced on the request still naming this
/// owner, and both phases ordered by game_url so two flushes over an
/// overlapping set queue behind each other instead of deadlocking half way
/// through.
class PgGameSink : public GameSink {
 public:
  PgGameSink(pg::Client& client, std::string request_id, std::string owner)
      : client_(client), request_id_(std::move(request_id)), owner_(std::move(owner)) {}

  absl::Status Write(absl::Span<const IndexedGame> games) override;

  absl::StatusOr<std::optional<int>> MonthAlreadyIndexed(const IndexedMonth& month) override;

  absl::Status RecordMonth(const IndexedMonth& month) override;

 private:
  pg::Client& client_;
  std::string request_id_;
  std::string owner_;
};

/// A sink with a connection of its own, for one run.
///
/// One pg::Client is one connection serialised by a mutex, so runs
/// sharing one queue every flush behind every other run's — and a flush
/// is the long part. Sharing it would leave the pool with nothing to
/// overlap.
std::unique_ptr<GameSink> NewOwnedPgGameSink(const std::string& db_url, std::string request_id,
                                             std::string owner);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H
