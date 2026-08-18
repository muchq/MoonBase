// Contract tests for the vendored chess-library (bazel/3p/chess_library).
//
// Every claim this library makes about what it does not have to implement
// rests on some behavior of chess-library, and none of those behaviors are
// promised by a version number. Pinned here, one named test each, so that
// updating the vendored header fails with "chess-library no longer
// classifies a discovered check" rather than with a corpus diff a thousand
// plies long.
//
// This is the same Beyoncé-rule move as //domains/games/apis/golf_hub's
// smithy contract test: if we like it, we put a test on it.
//
// These tests call chess:: directly and deliberately do not go through our
// wrappers — a wrapper bug and an upstream behavior change should not look
// alike.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "chess.hpp"

namespace {

// --- The PGN reader -----------------------------------------------------

/// Collects one game's headers and moves.
class Recorder : public chess::pgn::Visitor {
 public:
  void startPgn() override {}
  void header(std::string_view key, std::string_view value) override {
    headers.emplace_back(std::string(key), std::string(value));
  }
  void startMoves() override {}
  void move(std::string_view san, std::string_view comment) override {
    moves.emplace_back(san);
    if (!comment.empty()) comments.emplace_back(comment);
  }
  void endPgn() override { ++games; }

  std::vector<std::pair<std::string, std::string>> headers;
  std::vector<std::string> moves;
  std::vector<std::string> comments;
  int games = 0;
};

Recorder Read(const std::string& pgn) {
  std::istringstream stream(pgn);
  Recorder recorder;
  chess::pgn::StreamParser(stream).readGames(recorder);
  return recorder;
}

TEST(PgnReaderContract, DeliversMovesWithoutTheResultToken) {
  // Our pipeline has no "does this look like SAN" filter. The Java one
  // needs one because chariot hands back NAGs as moves; we rely on this
  // reader never emitting a non-move.
  const Recorder recorder = Read("[Event \"x\"]\n\n1. e4 e5 2. Nf3 1-0\n");
  EXPECT_EQ(recorder.moves, (std::vector<std::string>{"e4", "e5", "Nf3"}));
}

TEST(PgnReaderContract, DropsNagsAndSkipsVariations) {
  const Recorder recorder =
      Read("[Event \"x\"]\n\n1. e4 $1 e5 2. Nf3 (2. Bc4 Nf6 (2... d6)) 2... Nc6 *\n");
  EXPECT_EQ(recorder.moves, (std::vector<std::string>{"e4", "e5", "Nf3", "Nc6"}));
}

TEST(PgnReaderContract, DeliversCommentsSeparatelyFromMoves) {
  // chess.com writes a clock into every move. It has to arrive out of band
  // — and it is where per-move time control would come from later.
  const Recorder recorder = Read("[Event \"x\"]\n\n1. e4 {[%clk 0:02:57]} e5 *\n");
  EXPECT_EQ(recorder.moves, (std::vector<std::string>{"e4", "e5"}));
  EXPECT_THAT(recorder.comments, testing::ElementsAre("[%clk 0:02:57]"));
}

TEST(PgnReaderContract, ReportsNoErrorForTextThatIsNotPgn) {
  // Not a bug, but the reason ParseGame counts games itself: junk parses
  // as zero games and a clean status, which would otherwise read as "a
  // game with no moves".
  std::istringstream stream("this is not a pgn at all\n");
  Recorder recorder;
  const chess::pgn::StreamParserError error = chess::pgn::StreamParser(stream).readGames(recorder);
  EXPECT_FALSE(error.hasError());
  EXPECT_EQ(recorder.games, 0);
}

TEST(PgnReaderContract, KeepsUnknownHeadersRatherThanRejectingThem) {
  const Recorder recorder = Read("[Event \"x\"]\n[SomethingNew \"7\"]\n\n1. e4 *\n");
  EXPECT_THAT(recorder.headers,
              testing::Contains(std::make_pair(std::string("SomethingNew"), std::string("7"))));
}

TEST(PgnReaderContract, RaisesExceededMaxStringLengthForAnOversizedToken) {
  // Our own error text exists because of this code: StreamParserError's
  // message() has no case for it and falls into assert(false), so pgn.cc
  // never calls message(). Pinned here because the day upstream describes
  // it, that workaround can go — and because the code itself is what a
  // 255-plus-character tag in a real archive produces.
  std::istringstream stream("[Event \"" + std::string(300, 'x') + "\"]\n\n1. e4 *\n");
  Recorder recorder;
  const chess::pgn::StreamParserError error = chess::pgn::StreamParser(stream).readGames(recorder);
  EXPECT_TRUE(error.hasError());
  EXPECT_EQ(error.code(), chess::pgn::StreamParserError::ExceededMaxStringLength);
}

// --- SAN parsing --------------------------------------------------------

TEST(SanContract, RejectsAnIllegalMove) {
  // Replay() reports bad PGN on the strength of this throw. If the library
  // ever started returning NO_MOVE instead, a bad game would replay as a
  // shorter one.
  chess::Board board;
  EXPECT_THROW((void)chess::uci::parseSan(board, "Qh6"), chess::uci::SanParseError);
}

TEST(SanContract, ResolvesAmbiguityThatOnlyLegalityCanSettle) {
  // Rooks on d1 and d5 both reach d3 by movement, so "Rd3" names no file —
  // but the d1 rook is pinned to its king by the rook on a1, so exactly one
  // rook may legally go, and SAN allows the shorthand. This is the case a
  // movement table gets wrong and a rules engine gets right.
  chess::Board board("4k3/8/8/3R4/8/8/8/r2RK3 w - - 0 1");
  const chess::Move move = chess::uci::parseSan(board, "Rd3");
  EXPECT_EQ(move.from(), chess::Square("d5"));
}

TEST(SanContract, ResolvesRankDisambiguation) {
  // Two rooks on the same file: the rank digit picks one.
  chess::Board board("k7/8/8/4R3/8/8/8/4R1K1 w - - 0 1");
  EXPECT_EQ(chess::uci::parseSan(board, "R1e3").from(), chess::Square("e1"));
  EXPECT_EQ(chess::uci::parseSan(board, "R5e3").from(), chess::Square("e5"));
}

TEST(SanContract, ReadsPromotionAndUnderpromotion) {
  chess::Board board("8/4P3/8/8/8/8/8/K6k w - - 0 1");
  EXPECT_EQ(chess::uci::parseSan(board, "e8=Q").promotionType(), chess::PieceType::QUEEN);
  EXPECT_EQ(chess::uci::parseSan(board, "e8=N").promotionType(), chess::PieceType::KNIGHT);
}

// --- Position queries the detectors are built on ------------------------

TEST(BoardContract, AttackersNamesEveryAttackerOfASquare) {
  chess::Board board("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1");
  const chess::Bitboard checkers =
      chess::attacks::attackers(board, chess::Color::WHITE, board.kingSq(chess::Color::BLACK));
  EXPECT_EQ(checkers.count(), 1);
  EXPECT_TRUE(static_cast<bool>(checkers & chess::Bitboard::fromSquare(chess::Square("e2"))));
}

TEST(BoardContract, GivesCheckSeparatesDirectFromDiscovered) {
  // The whole Phase 9 case in #1389 — discovered check, double check, and
  // the promotion-with-check split — rests on this one classification.
  chess::Board direct("4k3/8/8/8/8/8/R7/4K3 w - - 0 1");
  EXPECT_EQ(direct.givesCheck(chess::uci::parseSan(direct, "Ra8")), chess::CheckType::DIRECT_CHECK);
  EXPECT_EQ(direct.givesCheck(chess::uci::parseSan(direct, "Ra3")), chess::CheckType::NO_CHECK);

  chess::Board discovered("4k3/8/8/8/4B3/8/8/4RK2 w - - 0 1");
  EXPECT_EQ(discovered.givesCheck(chess::uci::parseSan(discovered, "Bb1")),
            chess::CheckType::DISCOVERY_CHECK);
}

TEST(BoardContract, IsGameOverNamesCheckmateStalemateAndInsufficientMaterial) {
  EXPECT_EQ(chess::Board("6rk/5Npp/8/8/8/8/8/6K1 b - - 0 1").isGameOver().first,
            chess::GameResultReason::CHECKMATE);
  EXPECT_EQ(chess::Board("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1").isGameOver().first,
            chess::GameResultReason::STALEMATE);
  EXPECT_EQ(chess::Board("7k/8/6K1/8/8/8/8/8 w - - 0 1").isGameOver().first,
            chess::GameResultReason::INSUFFICIENT_MATERIAL);
}

TEST(BoardContract, UnmakeMoveRestoresThePositionExactly) {
  // The single-pass design walks a game forward and, for anything that
  // needs to look at a move in isolation, back again. Castling rights and
  // the en passant square are the parts that a naive undo loses.
  chess::Board board("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
  const std::string fen = board.getFen();
  const auto hash = board.hash();

  const chess::Move move = chess::uci::parseSan(board, "exf6");
  board.makeMove(move);
  ASSERT_NE(board.getFen(), fen);
  board.unmakeMove(move);

  EXPECT_EQ(board.getFen(), fen);
  EXPECT_EQ(board.hash(), hash);
}

TEST(BoardContract, FenOmitsTheEnPassantSquareWhenNoCaptureIsAvailable) {
  // The convention split corpus_test compares around, and the reason it
  // compares that field asymmetrically: chess.com writes the square after
  // any double push, this library writes it only when a capture is really
  // on. Both directions, so an upstream change to either fails here by
  // name instead of as a corpus diff.
  // Played out from the standard position rather than set up by hand, so
  // the fixtures cannot themselves be wrong about whose pawn is where.
  const auto play = [](std::initializer_list<const char*> sans) {
    chess::Board board;
    for (const char* san : sans) board.makeMove(chess::uci::parseSan(board, san));
    return board.getFen();
  };

  // 1. e4 — a double push with no black pawn on b4 or d4 to answer it.
  EXPECT_THAT(play({"e4"}), testing::HasSubstr(" - 0 1")) << "nothing can take on e3";

  // 1. e4 a6 2. e5 d5 — now the e5 pawn really can take on d6.
  EXPECT_THAT(play({"e4", "a6", "e5", "d5"}), testing::HasSubstr(" d6 "))
      << "the e5 pawn can take on d6";
}

TEST(BoardContract, FenCarriesHalfmoveAndFullmoveCounters) {
  // The corpus tests compare our replayed FEN against the one chess.com
  // wrote into [CurrentPosition], so the two have to agree field for field.
  const std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  EXPECT_EQ(chess::Board(fen).getFen(), fen);
}

TEST(BoardContract, LegalMovesExcludeMovesThatLeaveTheKingInCheck) {
  // Pseudo-legal generation would offer the pinned rook; this is what makes
  // "no legal moves" mean checkmate.
  chess::Board board("4k3/8/8/3R4/8/8/8/r2RK3 w - - 0 1");
  chess::Movelist moves;
  chess::movegen::legalmoves(moves, board);

  int from_d1 = 0;
  for (const chess::Move& move : moves) {
    if (move.from() == chess::Square("d1")) ++from_d1;
  }
  EXPECT_EQ(from_d1, 3) << "the pinned rook may only move along the rank it is pinned on";
}

}  // namespace
