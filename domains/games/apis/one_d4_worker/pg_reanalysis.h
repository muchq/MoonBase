#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_REANALYSIS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_REANALYSIS_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/reanalysis_run.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// The indexed corpus, paged by game_url.
class PgGameCorpus : public GameCorpus {
 public:
  explicit PgGameCorpus(pg::Client& client) : client_(client) {}

  absl::StatusOr<std::vector<StoredGame>> After(std::string_view after, int limit) override;

 private:
  pg::Client& client_;
};

/// Replaces occurrences for a batch of games, fenced on the pass's claim.
///
/// Fenced for the reason every write past ClaimNext is: a pass whose lease
/// went to somebody else must not still be writing rows under it.
class PgOccurrenceSink : public OccurrenceSink {
 public:
  PgOccurrenceSink(pg::Client& client, std::string request_id, std::string owner)
      : client_(client), request_id_(std::move(request_id)), owner_(std::move(owner)) {}

  /// FailedPrecondition when the claim is no longer ours — nothing is
  /// written in that case, since the fence runs inside the transaction.
  absl::Status Replace(const std::vector<ReanalyzedGame>& games) override;

 private:
  pg::Client& client_;
  std::string request_id_;
  std::string owner_;
};

/// The corpus and the sink for one pass, over a connection of their own.
///
/// Held together because they must share it: two connections would put the
/// paging read outside the transaction the replace runs in, which is
/// harmless but wasteful, and would double this thread's share of a
/// connection budget two other services draw on.
struct ReanalysisEnds {
  std::unique_ptr<pg::Client> client;
  std::unique_ptr<GameCorpus> corpus;
  std::unique_ptr<OccurrenceSink> sink;
};

ReanalysisEnds NewOwnedReanalysisEnds(const std::string& db_url, const std::string& request_id,
                                      const std::string& owner);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_REANALYSIS_H
