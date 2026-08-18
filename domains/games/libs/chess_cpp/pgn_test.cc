#include "domains/games/libs/chess_cpp/pgn.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"

namespace chess_cpp {
namespace {

constexpr char kGame[] = R"pgn([Event "Live Chess"]
[Site "Chess.com"]
[White "alice"]
[Black "bob"]
[Result "1-0"]
[CurrentPosition "8/8/8/8/8/8/8/K6k w - - 0 40"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0
)pgn";

std::string Game() { return kGame; }

TEST(ParseGame, ReadsHeadersInFileOrder) {
  const auto parsed = ParseGame(Game());
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  ASSERT_EQ(parsed->headers.size(), 6u);
  EXPECT_EQ(parsed->headers.entries().front().first, "Event");
  EXPECT_EQ(parsed->headers.Get("White"), "alice");
  EXPECT_EQ(parsed->headers.Get("CurrentPosition"), "8/8/8/8/8/8/8/K6k w - - 0 40");
  EXPECT_EQ(parsed->headers.Get("Missing"), std::nullopt);
}

TEST(ParseGame, HeaderLookupIsCaseSensitive) {
  // PGN tag names are case-sensitive, and treating them otherwise would
  // silently accept a file that means something else.
  const auto parsed = ParseGame(Game());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->headers.Get("white"), std::nullopt);
}

TEST(ParseGame, ReadsMovesAsSan) {
  const auto parsed = ParseGame(Game());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->san_moves, (std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bb5", "a6"}));
}

TEST(ParseGame, DropsResultTokenCommentsNagsAndVariations) {
  // The four things that are not moves but sit in the movetext. The Java
  // pipeline needs an isValidSan() filter because its tokenizer hands NAGs
  // back as moves; this asserts we inherit no such problem.
  const auto parsed = ParseGame(
      "[Event \"x\"]\n\n"
      "1. e4 {[%clk 0:03:00]} e5 $1 2. Nf3 (2. Bc4 Nf6 3. d3) 2... Nc6 1/2-1/2\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->san_moves, (std::vector<std::string>{"e4", "e5", "Nf3", "Nc6"}));
}

TEST(ParseGame, AcceptsGameWithNoMoves) {
  // An abandoned game is a real thing an archive contains, and it is not an
  // error — it is a game with nothing to replay.
  const auto parsed = ParseGame("[Event \"abandoned\"]\n[Result \"*\"]\n\n*\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_TRUE(parsed->san_moves.empty());
  EXPECT_EQ(parsed->headers.size(), 2u);
}

TEST(ParseGame, RejectsTextThatHoldsNoGame) {
  // The reader returns *no error* for text with no '[' in it, so without an
  // explicit count this would come back as a valid game with no moves —
  // indistinguishable from the abandoned game above.
  const auto parsed = ParseGame("this is not a pgn at all\n");
  EXPECT_EQ(parsed.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(parsed.status().message(), testing::HasSubstr("no game"));
}

TEST(ParseGame, RejectsMoreThanOneGame) {
  const auto parsed = ParseGame(Game() + "\n\n" + Game());
  EXPECT_EQ(parsed.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(parsed.status().message(), testing::HasSubstr("2 games"));
}

TEST(ParseGames, StreamsEveryGameInOrder) {
  std::istringstream stream(Game() + "\n\n[Event \"second\"]\n\n1. d4 d5 *\n");

  std::vector<std::string> events;
  std::vector<std::size_t> move_counts;
  const absl::Status status = ParseGames(stream, [&](ParsedGame game) {
    events.emplace_back(game.headers.Get("Event").value_or("?"));
    move_counts.push_back(game.san_moves.size());
    return absl::OkStatus();
  });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(events, (std::vector<std::string>{"Live Chess", "second"}));
  EXPECT_EQ(move_counts, (std::vector<std::size_t>{6, 2}));
}

TEST(ParseGames, StopsAtTheFirstSinkFailureAndReportsIt) {
  std::istringstream stream(Game() + "\n\n[Event \"second\"]\n\n1. d4 d5 *\n" +
                            "\n[Event \"third\"]\n\n1. c4 *\n");

  int seen = 0;
  const absl::Status status = ParseGames(stream, [&](ParsedGame) {
    ++seen;
    return seen == 2 ? absl::DataLossError("sink gave up") : absl::OkStatus();
  });

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "sink gave up");
  EXPECT_EQ(seen, 2) << "the third game should not have reached the sink";
}

TEST(ParseGames, ReportsAnOversizedTokenInsteadOfAborting) {
  // PGN string tokens cap at 255 characters in the reader, and the code it
  // raises past that is the one its own message() forgets — asking that
  // function to describe it runs into assert(false). So this input aborts
  // the process unless the description is ours, and it arrives from
  // somebody else's archive. Both halves of the reader that can raise it:
  // a header value, and a move token.
  const std::string too_long(300, 'x');

  for (const std::string& pgn :
       {"[Event \"" + too_long + "\"]\n\n1. e4 *\n", "[Event \"x\"]\n\n1. " + too_long + " *\n"}) {
    std::istringstream stream(pgn);
    const absl::Status status = ParseGames(stream, [](ParsedGame) { return absl::OkStatus(); });
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(status.message(), testing::HasSubstr("255 characters"));
  }
}

// --- StartFen -----------------------------------------------------------

TEST(StartFen, IsTheStandardPositionWhenNothingSaysOtherwise) {
  const auto parsed = ParseGame(Game());
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  const auto start = StartFen(parsed->headers);
  ASSERT_TRUE(start.ok()) << start.status();
  EXPECT_EQ(*start, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST(StartFen, IsTheFenTagForAnOddsGame) {
  // What chess.com sends for a game where one player spots the other a
  // piece: Black's f8 bishop is a second queen here.
  const auto parsed = ParseGame(
      "[Event \"x\"]\n[SetUp \"1\"]\n"
      "[FEN \"rnbqkqnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"]\n\n1. e4 *\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  const auto start = StartFen(parsed->headers);
  ASSERT_TRUE(start.ok()) << start.status();
  EXPECT_EQ(*start, "rnbqkqnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST(StartFen, TreatsSetUpZeroAsTheStandardPosition) {
  const auto parsed = ParseGame("[Event \"x\"]\n[SetUp \"0\"]\n\n1. e4 *\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  const auto start = StartFen(parsed->headers);
  ASSERT_TRUE(start.ok()) << start.status();
  EXPECT_EQ(*start, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST(StartFen, RejectsAFenWithoutASetUpTag) {
  // The dangerous half: guessing "standard" here would replay an odds game
  // from the wrong position and report its legal moves as illegal.
  const auto parsed = ParseGame(
      "[Event \"x\"]\n[FEN \"rnbqkqnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"]\n"
      "\n1. e4 *\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  const auto start = StartFen(parsed->headers);
  EXPECT_EQ(start.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(start.status().message(), testing::HasSubstr("disagree"));
}

TEST(StartFen, RejectsASetUpTagWithoutAFen) {
  const auto parsed = ParseGame("[Event \"x\"]\n[SetUp \"1\"]\n\n1. e4 *\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  const auto start = StartFen(parsed->headers);
  EXPECT_EQ(start.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace chess_cpp
