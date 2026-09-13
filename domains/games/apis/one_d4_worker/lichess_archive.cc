#include "domains/games/apis/one_d4_worker/lichess_archive.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "domains/games/libs/chess_cpp/pgn.h"

namespace one_d4_worker {
namespace {

constexpr std::string_view kEventTag = "[Event ";

std::string Tag(const chess_cpp::Headers& headers, std::string_view name) {
  const auto value = headers.Get(name);
  return value.has_value() ? std::string(*value) : "";
}

int TagAsInt(const chess_cpp::Headers& headers, std::string_view name) {
  int parsed = 0;
  return absl::SimpleAtoi(Tag(headers, name), &parsed) ? parsed : 0;
}

/// chess.com's per-side words for a standard result token, chosen so that
/// ResultOf() maps them back to the token we started from. The row stores
/// that notation, and both platforms have to reach it the same way.
void ResultWords(std::string_view result, std::string& white, std::string& black) {
  if (result == "1-0") {
    white = "win";
    black = "lose";
  } else if (result == "0-1") {
    white = "lose";
    black = "win";
  } else if (result == "1/2-1/2") {
    white = "drawn";
    black = "drawn";
  }
  // "*" and a missing tag leave both empty, which ResultOf reads as unknown.
}

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

/// Seconds since the epoch from the UTCDate and UTCTime tags. Zero when
/// either is missing or unparseable, matching the archive contract that a
/// field the source did not give is empty rather than a failure.
int64_t EndTimeFrom(const chess_cpp::Headers& headers) {
  const std::string date = Tag(headers, "UTCDate");
  const std::string time = Tag(headers, "UTCTime");
  if (date.empty() || time.empty()) return 0;
  absl::Time parsed;
  std::string error;
  if (!absl::ParseTime("%Y.%m.%d %H:%M:%S", absl::StrCat(date, " ", time), absl::UTCTimeZone(),
                       &parsed, &error)) {
    return 0;
  }
  return absl::ToUnixSeconds(parsed);
}

}  // namespace

std::vector<std::string_view> SplitPgnGames(std::string_view pgn) {
  std::vector<std::string_view> games;
  std::size_t start = pgn.find(kEventTag);
  while (start != std::string_view::npos) {
    std::size_t next = start + kEventTag.size();
    // A game ends where the next one's Event tag begins a line, so only a
    // match at the start of a line counts: "[Event " inside movetext or a
    // tag value is text, not a boundary.
    while (true) {
      next = pgn.find(kEventTag, next);
      if (next == std::string_view::npos || next == 0 || pgn[next - 1] == '\n') break;
      next += kEventTag.size();
    }
    const std::size_t end = next == std::string_view::npos ? pgn.size() : next;
    std::string_view game = pgn.substr(start, end - start);
    while (!game.empty() && absl::ascii_isspace(static_cast<unsigned char>(game.back()))) {
      game.remove_suffix(1);
    }
    if (!game.empty()) games.push_back(game);
    start = next;
  }
  return games;
}

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
    // Only the modeled 404 is NotFound, as on the chess.com side: everything
    // else is a failure to read the month, and completing the run on it
    // would record "indexed, no games" for a month nobody read (#1360).
    if (exported.error().code() == "GamesNotFound") return absl::NotFoundError(message);
    return absl::UnavailableError(message);
  }

  const std::string pgn = exported->games.ToString();
  std::vector<ArchivedGame> games;
  for (const std::string_view block : SplitPgnGames(pgn)) {
    ArchivedGame game;
    game.pgn = std::string(block);
    // A block that will not parse is still a game, and the run already has a
    // policy for one: IndexRun writes the row with no moves rather than
    // dropping it. Skipping here would pre-empt that — and if every block
    // failed, the month would come back an empty success and be recorded
    // complete, which is the quiet-month lie #1360 exists to prevent.
    const auto parsed = chess_cpp::ParseGame(block);
    if (parsed.ok()) {
      const chess_cpp::Headers& headers = parsed->headers;
      game.url = Tag(headers, "Site");
      game.white_username = Tag(headers, "White");
      game.black_username = Tag(headers, "Black");
      game.white_rating = TagAsInt(headers, "WhiteElo");
      game.black_rating = TagAsInt(headers, "BlackElo");
      game.time_class = TimeClassFrom(Tag(headers, "Event"));
      ResultWords(Tag(headers, "Result"), game.white_result, game.black_result);
      game.end_time = EndTimeFrom(headers);
      // Free here, unlike chess.com: the game states who was titled, so no
      // roster and no per-player lookup. Absent means untitled or unstated,
      // and empty carries that unchanged.
      game.white_title = Tag(headers, "WhiteTitle");
      game.black_title = Tag(headers, "BlackTitle");
      // eco_url stays empty — it is chess.com's slug. Lichess states the
      // name outright, which is what opening_name carries.
      game.opening_name = Tag(headers, "Opening");
    }
    games.push_back(std::move(game));
  }
  return games;
}

}  // namespace one_d4_worker
