#include "domains/games/apis/one_d4_worker/pgn_games.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

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

ArchivedGame ArchivedGameFromPgn(std::string_view block, TimeClassRule time_class) {
  ArchivedGame game;
  game.pgn = std::string(block);
  const auto parsed = chess_cpp::ParseGame(block);
  if (parsed.ok()) {
    const chess_cpp::Headers& headers = parsed->headers;
    game.url = Tag(headers, "Site");
    game.white_username = Tag(headers, "White");
    game.black_username = Tag(headers, "Black");
    game.white_rating = TagAsInt(headers, "WhiteElo");
    game.black_rating = TagAsInt(headers, "BlackElo");
    game.time_class = time_class(headers);
    ResultWords(Tag(headers, "Result"), game.white_result, game.black_result);
    game.end_time = EndTimeFrom(headers);
    // Free when the game states who was titled, unlike chess.com: no
    // roster and no per-player lookup. Absent means untitled or unstated,
    // and empty carries that unchanged.
    game.white_title = Tag(headers, "WhiteTitle");
    game.black_title = Tag(headers, "BlackTitle");
    // eco_url stays empty — it is chess.com's slug. A PGN export states
    // the name outright, when it states one, which is what opening_name
    // carries.
    game.opening_name = Tag(headers, "Opening");
  }
  return game;
}

}  // namespace one_d4_worker
