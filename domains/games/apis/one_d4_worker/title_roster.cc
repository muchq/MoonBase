#include "domains/games/apis/one_d4_worker/title_roster.h"

#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/time/clock.h"

namespace one_d4_worker {
namespace {

/// Every title chess.com publishes a roster for, strongest first.
///
/// One player can appear in two of them: they are ten documents read at
/// ten instants, so a title awarded in between lands in both, and the
/// first one read wins. chess.com's own profile field — which is what the
/// Java worker writes — carries the stronger of the two, so this is
/// ordered to agree with it rather than in chess.com's listing order,
/// which pairs each women's title with its open counterpart.
///
/// Strength here is the rating floor the title is awarded at: GM 2500, IM
/// 2400, FM and WGM 2300, CM and NM and WIM 2200, WFM 2100, WCM and WNM
/// 2000. Open title first where two share a floor.
constexpr std::string_view kTitles[] = {"GM", "IM",  "FM",  "WGM", "CM",
                                        "NM", "WIM", "WFM", "WCM", "WNM"};

}  // namespace

TitleRoster::TitleRoster(TitleSource& source, Options options)
    : source_(source), options_(std::move(options)) {}

absl::Time TitleRoster::Now() const { return options_.now ? options_.now() : absl::Now(); }

void TitleRoster::RefreshIfStale() {
  const absl::Time now = Now();
  if (loaded_ && now - refreshed_ < options_.good_for) return;
  if (now - attempted_ < options_.retry_after) return;

  // Stamped when the attempt ends rather than when it starts: ten calls
  // against a chess.com that is timing out can themselves take longer
  // than the backoff, and a backoff the attempt outlasts is no backoff.
  const absl::Cleanup stamp = [this] { attempted_ = Now(); };

  std::map<std::string, std::string, std::less<>> titles;
  for (const std::string_view title : kTitles) {
    if (Stopping()) return;
    const absl::StatusOr<std::vector<std::string>> players = source_.FetchTitled(title);
    // A partial set would title some players and not others, which reads
    // as a title being taken away. Keep what we had instead.
    if (!players.ok()) return;
    for (const std::string& player : *players) {
      titles.emplace(absl::AsciiStrToLower(player), std::string(title));
    }
  }

  // Ten rosters and nobody in any of them is not a state chess.com can be
  // in. Taking it would untitle the whole site while reporting nothing
  // wrong, and every month written under it would be cached that way.
  if (titles.empty()) return;

  titles_ = std::move(titles);
  loaded_ = true;
  refreshed_ = now;
}

bool TitleRoster::Stale() const { return !loaded_ || Now() - refreshed_ >= options_.good_for; }

bool TitleRoster::Stopping() const { return options_.stopping && options_.stopping(); }

absl::StatusOr<std::string> TitleRoster::TitleOf(std::string_view username) {
  RefreshIfStale();
  if (!loaded_) return absl::UnavailableError("no titled rosters to answer from");
  if (username.empty()) return "";

  const auto found = titles_.find(absl::AsciiStrToLower(username));
  return found == titles_.end() ? "" : found->second;
}

}  // namespace one_d4_worker
