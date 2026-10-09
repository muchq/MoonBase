#include "domains/games/libs/chess_play/pgn.h"

#include <algorithm>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "chess.hpp"

namespace chess_play {
namespace {

constexpr std::string_view kStandardFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
// Export format keeps movetext lines under eighty columns.
constexpr std::size_t kMaxLine = 79;

// The library's own UCI for the move `uci` names, the way GameState
// matches it, so castling reads the same here as when it was played.
chess::Move Find(const chess::Board& board, const std::string& uci) {
  chess::Movelist list;
  chess::movegen::legalmoves(list, board);
  const auto found = std::find_if(list.begin(), list.end(), [&](const chess::Move& move) {
    return chess::uci::moveToUci(move) == uci;
  });
  return found == list.end() ? chess::Move(chess::Move::NO_MOVE) : *found;
}

std::string Tag(std::string_view name, std::string_view value) {
  std::string escaped;
  for (const char c : value) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }
  return absl::StrCat("[", name, " \"", escaped, "\"]\n");
}

std::string ResultToken(const GameState& game) {
  if (!game.isOver()) return "*";
  const auto& winner = game.result()->winner;
  if (!winner.has_value()) return "1/2-1/2";
  return *winner == Color::kWhite ? "1-0" : "0-1";
}

std::string_view Termination(Ending ending) {
  switch (ending) {
    case Ending::kTimeout:
      return "time forfeit";
    case Ending::kAbandoned:
      return "abandoned";
    default:
      return "normal";
  }
}

}  // namespace

Replayed ReplayGame(const GameState& game) {
  chess::Board board(game.startFen());
  Replayed replayed;
  replayed.fens.push_back(board.getFen());
  for (const std::string& uci : game.moves()) {
    const chess::Move move = Find(board, uci);
    replayed.san.push_back(chess::uci::moveToSan(board, move));
    board.makeMove(move);
    replayed.fens.push_back(board.getFen());
  }
  return replayed;
}

std::string ToPgn(const GameState& game, const PgnTags& tags) {
  const std::string result = ResultToken(game);
  const absl::Time ended = absl::FromUnixMillis(tags.ended_at_ms);
  std::string pgn;
  absl::StrAppend(
      &pgn, Tag("Event", tags.event), Tag("Site", tags.site),
      Tag("Date", absl::FormatTime("%Y.%m.%d", ended, absl::UTCTimeZone())),
      Tag("Round", tags.round), Tag("White", game.players().at(game.seatOf(Color::kWhite))),
      Tag("Black", game.players().at(game.seatOf(Color::kBlack))), Tag("Result", result),
      Tag("UTCDate", absl::FormatTime("%Y.%m.%d", ended, absl::UTCTimeZone())),
      Tag("UTCTime", absl::FormatTime("%H:%M:%S", ended, absl::UTCTimeZone())));
  if (game.startFen() != kStandardFen) {
    absl::StrAppend(&pgn, Tag("SetUp", "1"), Tag("FEN", game.startFen()));
  }
  const TimeControl& clock = game.timeControl();
  absl::StrAppend(&pgn, Tag("TimeControl",
                            absl::StrCat(clock.initial_ms / 1000, "+", clock.increment_ms / 1000)));
  if (game.isOver()) absl::StrAppend(&pgn, Tag("Termination", Termination(game.result()->ending)));

  // Movetext: numbered from the start position's own move counter, with
  // "N..." when Black moves first.
  chess::Board board(game.startFen());
  const Replayed replayed = ReplayGame(game);
  int number = board.fullMoveNumber();
  bool white = board.sideToMove() == chess::Color::WHITE;
  std::vector<std::string> tokens;
  for (std::size_t i = 0; i < replayed.san.size(); ++i) {
    if (white) {
      tokens.push_back(absl::StrCat(number, ". ", replayed.san[i]));
    } else {
      if (i == 0)
        tokens.push_back(absl::StrCat(number, "... ", replayed.san[i]));
      else
        tokens.push_back(replayed.san[i]);
      ++number;
    }
    white = !white;
  }
  tokens.push_back(result);

  pgn += "\n";
  std::size_t line = 0;
  for (const std::string& token : tokens) {
    if (line > 0 && line + 1 + token.size() > kMaxLine) {
      pgn += "\n";
      line = 0;
    }
    if (line > 0) {
      pgn += " ";
      ++line;
    }
    pgn += token;
    line += token.size();
  }
  pgn += "\n";
  return pgn;
}

}  // namespace chess_play
