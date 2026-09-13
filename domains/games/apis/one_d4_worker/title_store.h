#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_STORE_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_STORE_H

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace one_d4_worker {

/// Lowercased username to title, which is how the roster holds it.
using TitleMap = std::map<std::string, std::string, std::less<>>;

/// One player's title as a single game stated it, dated by that game.
///
/// Lichess writes [WhiteTitle] / [BlackTitle]; chess.com writes neither for
/// anyone, which is why TitleRoster exists at all. A roster-derived title
/// never becomes one of these: the roster says what is true now, and filing
/// that under the game's own date would backdate a title the player may not
/// have held then.
struct TitleObservation {
  /// Lowercased, as the table keys it.
  std::string username;
  std::string title;
  /// Seconds since the epoch — when the game was played. Not the write
  /// time: indexing is not chronological, so a backfill of 2019 running
  /// after 2026 would otherwise demote a current GM.
  int64_t observed_at = 0;
};

/// Where titles outlive the process.
///
/// A port so TitleRoster can be tested without Postgres, and so the roster
/// itself stays a map probe: this is read once when a refresh has nothing
/// to install, not once per lookup.
class TitleStore {
 public:
  virtual ~TitleStore() = default;

  /// Records every pair as observed at `observed_at`. An observation never
  /// overwrites a newer one already stored, so writing an old month cannot
  /// demote a player the roster titled today.
  virtual absl::Status Save(std::string_view platform, const TitleMap& titles,
                            absl::Time observed_at) = 0;

  /// Every title stored for `platform`.
  virtual absl::StatusOr<TitleMap> Load(std::string_view platform) = 0;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_STORE_H
