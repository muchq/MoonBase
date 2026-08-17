#include "domains/games/libs/chess_cpp/replay.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/board_facts.h"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp {
namespace {

/// Replays and returns the FEN of every position, initial one included.
std::vector<std::string> Fens(const std::vector<std::string>& moves) {
  std::vector<std::string> fens;
  const absl::Status status =
      Replay(moves, [&](const Position& position) { fens.push_back(position.board.getFen()); });
  EXPECT_TRUE(status.ok()) << status;
  return fens;
}

/// Replays and returns only whether it worked — for the games that do not.
absl::Status ReplayStatus(const std::vector<std::string>& moves) {
  return Replay(moves, [](const Position&) {});
}

std::string FinalFen(const std::vector<std::string>& moves) {
  const std::vector<std::string> fens = Fens(moves);
  return fens.empty() ? "" : fens.back();
}

TEST(Replay, EmitsTheInitialPositionBeforeAnyMove) {
  const std::vector<std::string> fens = Fens({});

  ASSERT_EQ(fens.size(), 1u);
  EXPECT_EQ(fens.front(), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST(Replay, EmitsOnePositionPerMovePlusTheInitialOne) {
  EXPECT_EQ(Fens({"e4", "e5", "Nf3"}).size(), 4u);
}

TEST(Replay, NumbersPliesAndMovesTheWayPgnDoes) {
  std::vector<int> plies;
  std::vector<int> move_numbers;
  std::vector<std::string> movers;
  std::vector<std::string> to_move;

  const absl::Status status = Replay({"e4", "e5", "Nf3"}, [&](const Position& position) {
    plies.push_back(position.ply);
    move_numbers.push_back(position.move_number);
    to_move.emplace_back(ToString(position.side_to_move));
    if (position.last.has_value()) movers.emplace_back(ToString(position.last->by));
  });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(plies, (std::vector<int>{0, 1, 2, 3}));
  // Both halves of move 1 report 1; White's second move opens move 2.
  EXPECT_EQ(move_numbers, (std::vector<int>{0, 1, 1, 2}));
  EXPECT_EQ(to_move, (std::vector<std::string>{"white", "black", "white", "black"}));
  EXPECT_EQ(movers, (std::vector<std::string>{"white", "black", "white"}));
}

TEST(Replay, CarriesTheSanAndMoveThatProducedEachPosition) {
  std::vector<std::string> sans;
  std::vector<chess::Square> targets;
  const absl::Status status = Replay({"e4", "e5"}, [&](const Position& position) {
    if (position.ply == 0) {
      EXPECT_FALSE(position.last.has_value()) << "nothing was played to reach the start";
      return;
    }
    ASSERT_TRUE(position.last.has_value());
    sans.emplace_back(position.last->san);
    targets.push_back(position.last->move.to());
  });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(sans, (std::vector<std::string>{"e4", "e5"}));
  EXPECT_EQ(targets, (std::vector<chess::Square>{chess::Square("e4"), chess::Square("e5")}));
}

TEST(Replay, HandsOutThePositionEachMoveWasPlayedFrom) {
  // `before` is what makes facts::ClassifyCheck reachable from a callback,
  // and what every before/after detector in phase 2 will read. Checked
  // against the previous position's own FEN rather than against a
  // hand-written one, so this cannot drift from what the replayer emits.
  std::vector<std::string> after;
  std::vector<std::string> before;
  const absl::Status status = Replay({"e4", "e5", "Nf3"}, [&](const Position& position) {
    after.push_back(position.board.getFen());
    if (position.last.has_value()) before.push_back(position.last->before.getFen());
  });

  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(after.size(), 4u);
  ASSERT_EQ(before.size(), 3u);
  // The position a move was played from is the position before it.
  EXPECT_EQ(before[0], after[0]);
  EXPECT_EQ(before[1], after[1]);
  EXPECT_EQ(before[2], after[2]);
}

TEST(Replay, ThePreMoveBoardIsWhatClassifyCheckNeeds) {
  // The end this exists for: givesCheck is asked of the position before the
  // move, so a detector holding only a Position could not call it.
  std::vector<facts::CheckKind> kinds;
  const absl::Status status =
      Replay({"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7"}, [&](const Position& position) {
        if (!position.last.has_value()) return;
        kinds.push_back(facts::ClassifyCheck(position.last->before, position.last->move));
      });

  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(kinds.size(), 7u);
  EXPECT_EQ(kinds.back(), facts::CheckKind::kDirect) << "Qxf7 is mate, given directly";
  EXPECT_EQ(kinds.front(), facts::CheckKind::kNone) << "1. e4 checks nothing";
}

// --- The moves that break hand-rolled replayers -------------------------

TEST(Replay, KingsideAndQueensideCastlingForBothColours) {
  // Castling rights, rook relocation, and the king moving two squares —
  // White short, Black long. Read the expected position back as: Black king
  // on c8 with its rook on d8, White king on g1 with its rook on f1, both
  // sides' castling rights spent, and Black's dark bishop gone (traded on
  // e3, which is also why White has a doubled e-pawn).
  EXPECT_EQ(FinalFen({"e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "O-O", "Qf6", "d3", "d6", "Be3",
                      "Bxe3", "fxe3", "Bd7", "Nc3", "O-O-O"}),
            "2kr2nr/pppb1ppp/2np1q2/4p3/2B1P3/2NPPN2/PPP3PP/R2Q1RK1 w - - 3 9");
}

TEST(Replay, EnPassantCapture) {
  // The capture that removes a piece from a square the moving piece never
  // touches — and the one a placement-only board model gets wrong.
  EXPECT_EQ(FinalFen({"e4", "a6", "e5", "d5", "exd6"}),
            "rnbqkbnr/1pp1pppp/p2P4/8/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3");
}

TEST(Replay, PromotionToQueenAndUnderpromotionToKnight) {
  const std::vector<std::string> to_queen = {"e4", "d5",   "exd5", "Nf6",   "d6",
                                             "e5", "dxc7", "Bd6",  "cxb8=Q"};
  EXPECT_EQ(FinalFen(to_queen), "rQbqk2r/pp3ppp/3b1n2/4p3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 5");

  std::vector<std::string> to_knight = to_queen;
  to_knight.back() = "cxb8=N";
  EXPECT_EQ(FinalFen(to_knight), "rNbqk2r/pp3ppp/3b1n2/4p3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 5");
}

TEST(Replay, ResolvesFileDisambiguation) {
  // The Vienna: after 2...Nc6 both White knights — c3 and g1 — can reach
  // e2, so the move has to say which, and "Nge2" is the g1 one.
  EXPECT_EQ(FinalFen({"e4", "e5", "Nc3", "Nc6", "Nge2"}),
            "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N5/PPPPNPPP/R1BQKB1R b KQkq - 3 3");
}

TEST(Replay, HalfmoveClockResetsOnCaptureAndPawnMove) {
  // The clock is part of the FEN the corpus test compares, so it has to be
  // right and not merely present.
  EXPECT_EQ(Fens({"Nf3", "Nf6", "Ng1", "Ng8"}).back(),
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 4 3");
}

// --- Games that do not start from the standard position -----------------

TEST(ReplayFrom, TakesMoveNumberingFromTheStartingPosition) {
  // A game resumed at move 20 numbers its moves from 20. Counting plies
  // from one would misreport every occurrence in the game.
  std::vector<int> move_numbers;
  const absl::Status status =
      ReplayFrom("4k3/8/8/8/8/8/R7/4K3 w - - 4 20", {"Ra8", "Ke7"},
                 [&](const Position& position) { move_numbers.push_back(position.move_number); });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(move_numbers, (std::vector<int>{0, 20, 20}));
}

TEST(ReplayFrom, StartsWithBlackToMoveWhenTheFenSaysSo) {
  std::vector<std::string> to_move;
  const absl::Status status =
      ReplayFrom("4k3/8/8/8/8/8/R7/4K3 b - - 4 20", {"Ke7"},
                 [&](const Position& p) { to_move.emplace_back(ToString(p.side_to_move)); });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(to_move, (std::vector<std::string>{"black", "white"}));
}

TEST(ReplayFrom, PlaysAnOddsGameThatIsIllegalFromTheStandardStart) {
  // chess.com's odds games: this one starts Black a bishop down and a queen
  // up (f8 is a second queen), so "Qfe7" is a real move — and nonsense from
  // the standard position. tactics_corpus.pgn carries four of these.
  const std::string odds = "rnbqkqnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  const absl::Status from_standard = Replay({"e4", "e6", "d4", "Qfe7"}, [](const Position&) {});
  EXPECT_FALSE(from_standard.ok()) << "the point of the fixture is that this is not a normal game";

  const absl::Status status = ReplayFrom(odds, {"e4", "e6", "d4", "Qfe7"}, [](const Position&) {});
  EXPECT_TRUE(status.ok()) << status;
}

TEST(ReplayFrom, RejectsAPositionMissingAKing) {
  // Not a taste question: setFen builds castling paths as it parses, which
  // calls kingSq() for both colours, and kingSq() asserts on an empty king
  // bitboard — so this input aborts the process outright if it reaches the
  // library. It arrives from a [FEN] tag in somebody else's archive, so it
  // has to be rejected before that, and this test runs under the sanitize
  // job where the assert is live.
  for (const std::string_view kingless : {"8/8/8/8/8/8/8/4K3 w - - 0 1",   // no black king
                                          "4k3/8/8/8/8/8/8/8 w - - 0 1",   // no white king
                                          "8/8/8/8/8/8/8/8 w - - 0 1"}) {  // neither
    const absl::Status status = ReplayFrom(kingless, {}, [](const Position&) {});
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument) << kingless;
    EXPECT_THAT(status.message(), testing::HasSubstr("one king per side")) << kingless;
  }
}

TEST(ReplayFrom, RejectsAPositionWithTheSideNotToMoveInCheck) {
  // Unreachable in a real game, and the next move can capture a king.
  const absl::Status status =
      ReplayFrom("4k3/8/8/8/8/8/8/r2K4 b - - 0 1", {"Rxd1"}, [](const Position&) {});
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), testing::HasSubstr("not to move in check"));
}

TEST(ReplayFrom, RejectsTextThatIsNotAPosition) {
  const absl::Status status = ReplayFrom("not a fen at all", {"e4"}, [](const Position&) {});
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), testing::HasSubstr("not a position"));
}

// --- Failure reporting --------------------------------------------------

TEST(Replay, RejectsAnIllegalMoveNamingPlyMoveAndPosition) {
  const absl::Status status = ReplayStatus({"e4", "e5", "Qh6"});

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), testing::HasSubstr("2. Qh6"));
  EXPECT_THAT(status.message(), testing::HasSubstr("rnbqkbnr/pppp1ppp/8/4p3/4P3"));
}

TEST(Replay, ReportsBlackMovesWithTheEllipsisForm) {
  const absl::Status status = ReplayStatus({"e4", "Ke7"});
  EXPECT_THAT(status.message(), testing::HasSubstr("1... Ke7"));
}

TEST(Replay, RejectsTextThatIsNotAMove) {
  const absl::Status status = ReplayStatus({"e4", "$1"});
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Replay, StopsAtTheFailedMove) {
  int positions = 0;
  const absl::Status status =
      Replay({"e4", "e5", "Qh6", "Nc6"}, [&](const Position&) { ++positions; });
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(positions, 3) << "initial position plus the two moves that played";
}

}  // namespace
}  // namespace chess_cpp
