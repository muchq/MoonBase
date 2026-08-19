#include "domains/games/apis/one_d4_worker/title_roster.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/time/clock.h"

namespace one_d4_worker {
namespace {

/// Strongest first, because one player can appear in two rosters: they
/// are ten documents read at ten instants, so a title awarded in between
/// lands in both. Order makes the answer the same every time.
constexpr std::string_view kTitles[] = {"GM",  "WGM", "IM",  "WIM", "FM",
                                        "WFM", "NM",  "WNM", "CM",  "WCM"};

}  // namespace

TitleRoster::TitleRoster(TitleSource& source, Options options)
    : source_(source), options_(std::move(options)) {}

absl::Time TitleRoster::Now() const { return options_.now ? options_.now() : absl::Now(); }

void TitleRoster::RefreshIfStale() {
  const absl::Time now = Now();
  if (loaded_ && now - refreshed_ < options_.good_for) return;
  if (now - attempted_ < options_.retry_after) return;
  attempted_ = now;

  std::map<std::string, std::string, std::less<>> titles;
  for (const std::string_view title : kTitles) {
    const absl::StatusOr<std::vector<std::string>> players = source_.FetchTitled(title);
    // A partial set would title some players and not others, which reads
    // as a title being taken away. Keep what we had instead.
    if (!players.ok()) return;
    for (const std::string& player : *players) {
      titles.emplace(absl::AsciiStrToLower(player), std::string(title));
    }
  }

  titles_ = std::move(titles);
  loaded_ = true;
  refreshed_ = now;
}

absl::StatusOr<std::string> TitleRoster::TitleOf(std::string_view username) {
  RefreshIfStale();
  if (!loaded_) return absl::UnavailableError("no titled rosters to answer from");
  if (username.empty()) return "";

  const auto found = titles_.find(absl::AsciiStrToLower(username));
  return found == titles_.end() ? "" : found->second;
}

}  // namespace one_d4_worker
