// Replays two frozen banks of real chess.com games and checks every game's
// final position against the one chess.com itself recorded in the PGN's
// [CurrentPosition] header.
//
// The oracle matters more than the size: [CurrentPosition] is ground truth
// this repo did not compute, from a server that played the games. A replayer
// checked against a golden file it generated agrees with itself.
//
// The two banks answer different questions:
//
//   hikaru_corpus.pgn   500 games, one player, one month, blitz + bullet.
//                       Wide net over ordinary play. Shared with the Java
//                       pipeline's HikaruCorpusParityTest, which is why the
//                       ply count below is that test's constant: two
//                       independent tokenizers over one file have to agree
//                       on how many moves are in it.
//
//   tactics_corpus.pgn  150 games, different players, selected for the moves
//                       that break replayers — underpromotion, promotion
//                       with check, mate — plus whatever en passant came
//                       along. See testdata/build_tactics_corpus.py.
//
// Ordinary play turns out to be a poor test of a replayer: one 709-game
// archive held a single underpromotion. Hence the second bank.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/board_facts.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp {
namespace {

/// The Java pipeline's HikaruCorpusParityTest asserts this same number over
/// the same file. A tokenizer that drops or invents a move moves it.
constexpr int kHikaruGames = 500;
constexpr int kHikaruPlies = 42'706;

/// What a corpus turned out to contain. Counted rather than assumed: a
/// regenerated bank that quietly stopped containing underpromotions would
/// still pass every other assertion in this file.
struct Coverage {
  int games = 0;
  int plies = 0;
  int en_passant = 0;
  int castles = 0;
  int promotions = 0;
  int underpromotions = 0;
  int checkmates = 0;
  int double_checks = 0;
  int custom_starts = 0;
};

/// How a game identifies itself in a failure message.
std::string Label(const ParsedGame& game, int index) {
  if (const auto link = game.headers.Get("Link")) return std::string(*link);
  if (const auto url = game.headers.Get("Site")) {
    return absl::StrCat(*url, " game ", index, " (", game.headers.Get("White").value_or("?"),
                        " vs ", game.headers.Get("Black").value_or("?"), ")");
  }
  return absl::StrCat("game ", index);
}

/// True when a pawn of the side to move stands beside the pawn that just
/// double-pushed to make `ep_square` an en passant target.
///
/// Deliberately reads the placement rather than asking the board whether an
/// en passant capture is available. The board would answer from the same
/// `ep_sq_` field that decided not to print the square in the first place,
/// so the check would agree with itself by construction and could only ever
/// catch a getFen() formatting slip. Counting the pawns is independent of
/// that state, so it still fails if a makeMove ever stops recording the
/// square.
bool APawnStandsBesideTheDoublePushedPawn(const chess::Board& board, std::string_view ep_square) {
  const chess::Square target(ep_square);
  // The pawn that pushed sits on the rank the target square was skipped
  // over from: rank 5 for a target on rank 6, rank 4 for one on rank 3.
  const int pushed_rank = target.rank() == chess::Rank::RANK_6 ? 4 : 3;
  const int file = target.file();

  const chess::Bitboard capturers = board.pieces(chess::PieceType::PAWN, board.sideToMove());
  for (const int beside : {file - 1, file + 1}) {
    if (beside < 0 || beside > 7) continue;
    if (capturers & chess::Bitboard::fromSquare(chess::Square(beside + pushed_rank * 8))) {
      return true;
    }
  }
  return false;
}

/// Compares a replayed FEN against chess.com's, field by field.
///
/// Every field is compared exactly except en passant, where the two
/// disagree by convention rather than by fact: chess.com writes the square
/// after any double push, and chess-library writes it only when a capture
/// is actually available (pinned in chess_library_contract_test). So a
/// square we did print must match, and a square we did not print is
/// forgiven only when no enemy pawn was standing next to the pawn that
/// pushed — which is the convention difference itself, and not a licence to
/// lose a square somebody could have captured on.
void ExpectFenMatches(const chess::Board& board, std::string_view expected,
                      std::string_view label) {
  const std::vector<std::string> ours = absl::StrSplit(board.getFen(), ' ');
  const std::vector<std::string> theirs = absl::StrSplit(expected, ' ');
  ASSERT_EQ(ours.size(), 6u) << label;
  ASSERT_EQ(theirs.size(), 6u) << label << ": [CurrentPosition] is not a full FEN: " << expected;

  EXPECT_EQ(ours[0], theirs[0]) << "placement, " << label;
  EXPECT_EQ(ours[1], theirs[1]) << "side to move, " << label;
  EXPECT_EQ(ours[2], theirs[2]) << "castling rights, " << label;
  EXPECT_EQ(ours[4], theirs[4]) << "halfmove clock, " << label;
  EXPECT_EQ(ours[5], theirs[5]) << "fullmove number, " << label;

  if (ours[3] != "-") {
    EXPECT_EQ(ours[3], theirs[3]) << "en passant square, " << label;
  } else if (theirs[3] != "-") {
    EXPECT_FALSE(APawnStandsBesideTheDoublePushedPawn(board, theirs[3]))
        << "we dropped an en passant square a pawn was in position to take, " << label;
  }
}

Coverage ReplayCorpus(const std::string& path) {
  std::ifstream stream(path);
  EXPECT_TRUE(stream.is_open())
      << "cannot open " << path
      << " — a cc_test reads its data relative to the runfiles root, so this is a missing "
         "`data` entry rather than a missing file";

  Coverage coverage;
  const absl::Status status = ParseGames(stream, [&](ParsedGame game) {
    const std::string label = Label(game, coverage.games);
    ++coverage.games;

    // Odds games — chess.com's, where a player spots the other a piece —
    // start somewhere other than the standard position and say so in their
    // headers. Four of them are in this bank on purpose.
    const absl::StatusOr<std::string_view> start_fen = StartFen(game.headers);
    EXPECT_TRUE(start_fen.ok()) << label << ": " << start_fen.status().message();
    if (!start_fen.ok()) return absl::OkStatus();
    if (*start_fen != chess::constants::STARTPOS) ++coverage.custom_starts;

    const auto expected = game.headers.Get("CurrentPosition");
    EXPECT_TRUE(expected.has_value()) << label << ": no [CurrentPosition] header";

    // The last ply is known up front, so the final position can be checked
    // where it stands rather than copied out of the callback. Copying a
    // board per ply to keep hold of the last one costs 42,706 copies on the
    // bigger bank and reads like the replayer hands back less than it does.
    const int last_ply = static_cast<int>(game.san_moves.size());
    bool checked_final = false;

    const auto on_position = [&](const Position& position) {
      if (position.ply == last_ply && expected.has_value()) {
        checked_final = true;
        ExpectFenMatches(position.board, *expected, label);
      }
      if (position.ply == 0) return;

      ++coverage.plies;
      const chess::Move move = position.last->move;
      switch (move.typeOf()) {
        case chess::Move::ENPASSANT:
          ++coverage.en_passant;
          break;
        case chess::Move::CASTLING:
          ++coverage.castles;
          break;
        case chess::Move::PROMOTION:
          ++coverage.promotions;
          if (move.promotionType() != chess::PieceType::QUEEN) ++coverage.underpromotions;
          break;
        default:
          break;
      }
      if (facts::InDoubleCheck(position.board, position.side_to_move)) ++coverage.double_checks;
      if (position.board.isGameOver().first == chess::GameResultReason::CHECKMATE) {
        ++coverage.checkmates;
      }
    };

    const absl::Status replayed = ReplayFrom(*start_fen, game.san_moves, on_position);

    EXPECT_TRUE(replayed.ok()) << label << ": " << replayed.message();
    // A game that replayed but never reached its last ply would slip past
    // the position check entirely, so the check itself is checked.
    if (replayed.ok() && expected.has_value()) {
      EXPECT_TRUE(checked_final) << label << ": final position was never compared";
    }
    return absl::OkStatus();
  });

  EXPECT_TRUE(status.ok()) << path << ": " << status.message();
  return coverage;
}

// --- hikaru_corpus.pgn --------------------------------------------------

TEST(HikaruCorpus, ReplaysEveryGameToThePositionChessComRecorded) {
  const Coverage coverage =
      ReplayCorpus("domains/games/apis/one_d4/src/test/resources/hikaru_corpus.pgn");

  EXPECT_EQ(coverage.games, kHikaruGames);
  // Same file, same count as the Java pipeline asserts. Two tokenizers, one
  // number: if they ever disagree, one of them is reading moves the other
  // is not.
  EXPECT_EQ(coverage.plies, kHikaruPlies);
}

// --- tactics_corpus.pgn -------------------------------------------------

TEST(TacticsCorpus, ReplaysEveryGameToThePositionChessComRecorded) {
  const Coverage coverage =
      ReplayCorpus("domains/games/libs/chess_cpp/testdata/tactics_corpus.pgn");
  EXPECT_EQ(coverage.games, 150);
  EXPECT_GT(coverage.plies, 10'000);
}

TEST(TacticsCorpus, ContainsTheEdgeCasesItWasSelectedFor) {
  // Floors, not exact counts: the bank is regenerable (build_tactics_corpus.py)
  // and archives grow. What must not happen silently is a rebuild that drops
  // the rare cases and leaves a corpus that only proves ordinary moves work.
  const Coverage coverage =
      ReplayCorpus("domains/games/libs/chess_cpp/testdata/tactics_corpus.pgn");

  EXPECT_GE(coverage.underpromotions, 5) << "selected for, tier 2";
  EXPECT_GE(coverage.promotions, 60) << "selected for, tier 3";
  EXPECT_GE(coverage.checkmates, 60) << "selected for, tier 4";
  // Not selectable — en passant has no SAN marker — so this is the check
  // that the bank happens to cover it at all. If a rebuild lands here, the
  // selection needs a tier that can find it.
  EXPECT_GE(coverage.en_passant, 5) << "incidental, but the bank is worth less without it";
  EXPECT_GE(coverage.castles, 100);
  // Odds games, which the Java pipeline cannot replay at all. Keeping them
  // in the bank is the point: they are what ReplayFrom() exists for.
  // A floor, like the others: the bank is regenerable and this is what
  // stops a rebuild from dropping the only games that exercise ReplayFrom.
  // Also a floor, and for the same reason: a rebuild that dropped these
  // would leave nothing exercising ReplayFrom at all.
  EXPECT_GE(coverage.custom_starts, 3);
}

}  // namespace
}  // namespace chess_cpp
