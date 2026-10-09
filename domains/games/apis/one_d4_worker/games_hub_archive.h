#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAMES_HUB_ARCHIVE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAMES_HUB_ARCHIVE_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "moonbase/games/client.h"
#include "opal/client/config.h"

namespace one_d4_worker {

/// muchq.com's own chess games (#1637), from games_hub's public feed.
///
/// The feed is every game that ended in a published room over the last
/// 30 days, in archive order, not one player's month. So a month is the
/// whole feed narrowed here: the player on either side, ended inside the
/// month. Reading it whole is also why there is no cursor to keep and no
/// overlap to dedupe — a game that lands behind one already read is in the
/// next read.
///
/// One read serves every month and every slot for a minute. The hub allows
/// a client 20 requests a minute, and a twelve-month request reading the
/// feed per month would spend them all on one player.
///
/// A month older than the feed's 30 days comes back with what the feed
/// still holds, and is recorded complete. The hub keeps the rest in its own
/// archive, but nothing public serves it.
class GamesHubArchive : public ArchiveSource {
 public:
  /// Retries for long enough to ride out a games_hub restart, which every
  /// deploy is.
  static opal::ClientConfig DefaultClientConfig(std::string endpoint);
  static absl::StatusOr<std::unique_ptr<GamesHubArchive>> Create(
      opal::ClientConfig config, std::function<absl::Time()> now = absl::Now);

  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                       YearMonth month) override;

 private:
  GamesHubArchive(moonbase::games::GamesHubClient client, std::function<absl::Time()> now)
      : client_(std::move(client)), now_(std::move(now)) {}

  /// Every game in the feed, page after page until one comes back empty.
  absl::StatusOr<std::vector<ArchivedGame>> ReadFeed() const;

  moonbase::games::GamesHubClient client_;
  std::function<absl::Time()> now_;
  absl::Mutex mu_;
  std::vector<ArchivedGame> feed_ ABSL_GUARDED_BY(mu_);
  absl::Time read_at_ ABSL_GUARDED_BY(mu_) = absl::InfinitePast();
};

/// The speed of a PGN TimeControl ("180+2"), by Lichess's rule: initial
/// seconds plus forty increments. "" when the clock will not read.
std::string TimeClassOfControl(std::string_view control);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_GAMES_HUB_ARCHIVE_H
