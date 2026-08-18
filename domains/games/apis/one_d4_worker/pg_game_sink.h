#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H

#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

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

  absl::Status RecordMonth(const IndexedMonth& month) override;

 private:
  pg::Client& client_;
  std::string request_id_;
  std::string owner_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_GAME_SINK_H
