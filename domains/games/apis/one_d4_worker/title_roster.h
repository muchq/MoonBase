#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_ROSTER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_ROSTER_H

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace one_d4_worker {

/// Where the titled rosters come from.
class TitleSource {
 public:
  virtual ~TitleSource() = default;

  /// Every player holding `title`.
  virtual absl::StatusOr<std::vector<std::string>> FetchTitled(std::string_view title) = 0;
};

/// Who holds a title, for the whole site.
///
/// Titles are a column queries ask about — `opponent.title = "GM"` is a
/// documented one — but almost nobody has one, so asking chess.com per
/// opponent spends a round trip to learn "no" nearly every time. The
/// rosters are ten documents and a few hundred kilobytes for the entire
/// titled population, so this reads those instead and answers from
/// memory.
///
/// One roster serves the whole pool, so every lookup takes the lock —
/// and a refresh holds it for as long as ten sequential calls take. That
/// is a few seconds a day against a lookup that is a map probe, and it
/// is what stops several runs refreshing at once when the day turns.
class TitleRoster {
 public:
  struct Options {
    /// How long a roster is trusted. Titles are awarded on a scale of
    /// months, so a day is generous rather than eager.
    absl::Duration good_for = absl::Hours(24);

    /// How long to wait after a failed refresh before trying again.
    /// Without it a bad minute becomes ten failed requests per lookup —
    /// the fan-out this exists to remove, with nothing to show for it.
    absl::Duration retry_after = absl::Minutes(5);

    /// Whether the worker is shutting down. A refresh is ten sequential
    /// calls, so without this a SIGTERM arriving at the first one is not
    /// noticed until the tenth returns — long past the grace period, and
    /// a killed process hands its claim back to nobody.
    std::function<bool()> stopping;

    std::function<absl::Time()> now;
  };

  TitleRoster(TitleSource& source, Options options);

  /// The player's title, or "" when they hold none.
  ///
  /// Fails only when there is no roster to answer from at all. A stale
  /// one still answers: a title this worker has not heard about yet is a
  /// smaller wrong than every player in the month losing the one it had.
  absl::StatusOr<std::string> TitleOf(std::string_view username);

  /// True when what it answers with is older than `good_for` — a refresh
  /// came due and did not land.
  ///
  /// Answering from a stale roster is the right trade, but the month it
  /// was answered for is not complete: chess.com being down for a week
  /// would otherwise write a week of months whose titles nobody will ever
  /// look at again, because complete months are never refetched.
  bool Stale() const;

 private:
  absl::Time Now() const;
  bool Stopping() const;
  void RefreshIfStale() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  TitleSource& source_;
  const Options options_;

  /// Lowercased username to title. The rosters come back lowercase and an
  /// archive names the same player however they typed it.
  mutable absl::Mutex mu_;
  std::map<std::string, std::string, std::less<>> titles_ ABSL_GUARDED_BY(mu_);
  bool loaded_ ABSL_GUARDED_BY(mu_) = false;
  absl::Time refreshed_ ABSL_GUARDED_BY(mu_) = absl::InfinitePast();
  absl::Time attempted_ ABSL_GUARDED_BY(mu_) = absl::InfinitePast();
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_TITLE_ROSTER_H
