#include "domains/games/apis/one_d4_worker/lichess_archive.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "domains/games/apis/one_d4_worker/pgn_games.h"
#include "domains/games/libs/chess_cpp/pgn.h"

namespace one_d4_worker {
namespace {

/// The speed, from the Event tag: "Rated Blitz game", "Casual UltraBullet
/// game", "Rated Blitz tournament https://...".
///
/// UltraBullet stays itself rather than collapsing into bullet. It is a
/// category chess.com does not have, and `exclude_bullet` currently tests
/// `time_class == "bullet"` — so a request that excludes bullet keeps
/// ultrabullet games. That is the behaviour this makes visible rather than
/// decides; see #1527's open question.
///
/// Correspondence does become chess.com's "daily", which is the same thing
/// under another name — normalising a name is this function's job, merging
/// two categories is not.
std::string TimeClassFrom(std::string_view event) {
  const std::string lowered = absl::AsciiStrToLower(event);
  if (absl::StrContains(lowered, "ultrabullet")) return "ultrabullet";
  if (absl::StrContains(lowered, "bullet")) return "bullet";
  if (absl::StrContains(lowered, "blitz")) return "blitz";
  if (absl::StrContains(lowered, "rapid")) return "rapid";
  if (absl::StrContains(lowered, "classical")) return "classical";
  if (absl::StrContains(lowered, "correspondence")) return "daily";
  return "";
}

std::string TimeClassOf(const chess_cpp::Headers& headers) {
  const auto event = headers.Get("Event");
  return TimeClassFrom(event.has_value() ? *event : "");
}

}  // namespace

absl::StatusOr<std::vector<ArchivedGame>> LichessArchive::FetchMonth(std::string_view player,
                                                                     YearMonth month) {
  const int64_t since_ms = month.FirstInstant() * 1000;
  const int64_t until_ms = month.Next().FirstInstant() * 1000;
  const auto exported = [&] {
    const absl::MutexLock lock(one_at_a_time_);
    return client_.ExportGames(player, since_ms, until_ms);
  }();
  if (!exported.ok()) {
    const std::string message =
        absl::StrCat("lichess games ", player, " ", month.ToString(), ": ", exported.error().code(),
                     ": ", exported.error().message());
    // A token Lichess refuses, which is the deployment's to fix whether it
    // is revoked, expired or mistyped.
    if (exported.error().code() == "InvalidToken") return absl::UnauthenticatedError(message);
    // Only the modeled 404 is NotFound, as on the chess.com side: everything
    // else is a failure to read the month, and completing the run on it
    // would record "indexed, no games" for a month nobody read (#1360).
    if (exported.error().code() == "GamesNotFound") {
      // Every export a tokenless worker makes comes back this way, whoever
      // the player is, so on such a worker the 404 says nothing about the
      // player and everything about the deployment.
      if (!client_.authenticated()) return absl::UnauthenticatedError(message);
      return absl::NotFoundError(message);
    }
    return absl::UnavailableError(message);
  }

  const std::string pgn = exported->games.ToString();
  std::vector<ArchivedGame> games;
  for (const std::string_view block : SplitPgnGames(pgn)) {
    games.push_back(ArchivedGameFromPgn(block, TimeClassOf));
  }
  return games;
}

}  // namespace one_d4_worker
