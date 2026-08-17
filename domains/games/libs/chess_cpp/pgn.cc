#include "domains/games/libs/chess_cpp/pgn.h"

#include <istream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/parsed_game.h"

namespace chess_cpp {
namespace {

/// Assembles one ParsedGame per game and hands it to the sink.
///
/// The sink's failure has to be remembered rather than thrown: the reader
/// calls us back through a void interface and offers no way to stop it. So
/// the first failure latches, and every later game is skipped through
/// skipPgn() — which suppresses its headers and moves and leaves only the
/// endPgn callback. Parsing still walks the rest of the stream, but it does
/// no work per game, and the caller gets the first error rather than the
/// last.
class CollectingVisitor : public chess::pgn::Visitor {
 public:
  explicit CollectingVisitor(absl::FunctionRef<absl::Status(ParsedGame)> on_game)
      : on_game_(on_game) {}

  void startPgn() override {
    if (!status_.ok()) {
      skipPgn(true);
      return;
    }
    game_ = ParsedGame{};
  }

  void header(std::string_view name, std::string_view value) override {
    game_.headers.Add(std::string(name), std::string(value));
  }

  void startMoves() override {}

  void move(std::string_view san, std::string_view /*comment*/) override {
    game_.san_moves.emplace_back(san);
  }

  void endPgn() override {
    if (!status_.ok()) return;
    status_ = on_game_(std::move(game_));
  }

  const absl::Status& status() const { return status_; }

 private:
  absl::FunctionRef<absl::Status(ParsedGame)> on_game_;
  ParsedGame game_;
  absl::Status status_ = absl::OkStatus();
};

}  // namespace

absl::Status ParseGames(std::istream& stream, absl::FunctionRef<absl::Status(ParsedGame)> on_game) {
  CollectingVisitor visitor(on_game);
  const chess::pgn::StreamParserError error = chess::pgn::StreamParser(stream).readGames(visitor);

  // The sink's error outranks the reader's. A sink that failed on game 3 is
  // the reason we stopped caring about the rest of the file, so reporting a
  // truncation at the end instead would name the symptom.
  if (!visitor.status().ok()) return visitor.status();
  if (error.hasError()) {
    return absl::InvalidArgumentError(absl::StrCat("malformed pgn: ", error.message()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string_view> StartFen(const Headers& headers) {
  const std::optional<std::string_view> set_up = headers.Get("SetUp");
  const std::optional<std::string_view> fen = headers.Get("FEN");

  // [SetUp "0"] is the spec's way of saying "standard", so it is only the
  // "1" that promises a [FEN].
  const bool custom = set_up.has_value() && *set_up == "1";
  if (custom != fen.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("[SetUp] and [FEN] disagree: SetUp=", set_up.value_or("(absent)"),
                     " FEN=", fen.has_value() ? "present" : "(absent)"));
  }
  return custom ? *fen : std::string_view(chess::constants::STARTPOS);
}

absl::StatusOr<ParsedGame> ParseGame(std::string_view pgn) {
  std::istringstream stream{std::string(pgn)};
  ParsedGame parsed;
  int count = 0;
  absl::Status status = ParseGames(stream, [&](ParsedGame game) {
    if (++count == 1) parsed = std::move(game);
    return absl::OkStatus();
  });
  if (!status.ok()) return status;

  // Text that is not a PGN at all parses as zero games without an error —
  // the reader is looking for a '[' and reaches the end without finding
  // one. Caught here so a caller cannot mistake junk for a game with no
  // moves, which is a real thing an abandoned game looks like.
  if (count == 0) return absl::InvalidArgumentError("pgn holds no game");
  if (count > 1) {
    return absl::InvalidArgumentError(absl::StrCat("pgn holds ", count, " games, expected 1"));
  }
  return parsed;
}

}  // namespace chess_cpp
