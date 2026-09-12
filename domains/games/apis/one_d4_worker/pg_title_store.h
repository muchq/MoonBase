#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TITLE_STORE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TITLE_STORE_H

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/title_store.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// player_titles, as TitleRoster wants it.
class PgTitleStore : public TitleStore {
 public:
  /// Rows per INSERT. A roster is tens of thousands of players and
  /// pg::Client binds every value as a parameter, so this is sent in
  /// batches rather than one statement with a hundred thousand of them.
  static constexpr int kBatchRows = 500;

  explicit PgTitleStore(pg::Client& client) : client_(client) {}

  absl::Status Save(std::string_view platform, const TitleMap& titles,
                    absl::Time observed_at) override;

  absl::StatusOr<TitleMap> Load(std::string_view platform) override;

 private:
  pg::Client& client_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TITLE_STORE_H
