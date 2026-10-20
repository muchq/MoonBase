#include "domains/games/libs/go/sgf.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace go {
namespace {

// SimpleText: a backslash quotes the next character, and ] would close it.
std::string Text(std::string_view text) {
  std::string quoted;
  for (const char c : text) {
    if (c == ']' || c == '\\') quoted += '\\';
    quoted += c;
  }
  return quoted;
}

std::string ResultOf(const GameState& game) {
  const Result& result = *game.result();
  if (!result.winner.has_value()) return "0";
  const std::string winner = *result.winner == Color::kBlack ? "B+" : "W+";
  switch (result.ending) {
    case Ending::kScore: {
      const Score score = game.score();
      return absl::StrCat(winner, std::abs(score.black - score.white));
    }
    case Ending::kResignation:
      return winner + "R";
    case Ending::kTimeout:
      return winner + "T";
    case Ending::kAbandoned:
      return winner + "F";
  }
  return winner;
}

// Columns from the left and rows from the top, as a point's x and y run.
std::string Point(int move, int size) {
  if (move == kPass) return "";
  return {static_cast<char>('a' + move % size), static_cast<char>('a' + move / size)};
}

}  // namespace

std::string ToSgf(const GameState& game, int64_t ended_at_ms) {
  std::string sgf = absl::StrCat(
      "(;FF[4]GM[1]CA[UTF-8]SZ[", game.size(), "]KM[", game.komi(), "]PB[",
      Text(game.players()[game.seatOf(Color::kBlack)]), "]PW[",
      Text(game.players()[game.seatOf(Color::kWhite)]), "]DT[",
      absl::FormatTime("%Y-%m-%d", absl::FromUnixMillis(ended_at_ms), absl::UTCTimeZone()), "]");
  if (game.isOver()) absl::StrAppend(&sgf, "RE[", ResultOf(game), "]");
  Color mover = Color::kBlack;
  for (const int move : game.moves()) {
    absl::StrAppend(&sgf, mover == Color::kBlack ? ";B[" : ";W[", Point(move, game.size()), "]");
    mover = Other(mover);
  }
  return sgf + ")";
}

}  // namespace go
