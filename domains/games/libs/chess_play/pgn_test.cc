#include "domains/games/libs/chess_play/pgn.h"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/game_state_serde.h"

namespace chess_play {
namespace {

constexpr char kStandard[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr int64_t kT0 = 1'000'000;
const TimeControl kClock{180'000, 2'000};
// 2026-10-07T12:00:00Z.
constexpr int64_t kEndedAt = 1'791'374'400'000;

GameState Started(int white_seat = 0, const char* fen = kStandard, const char* variant = "standard",
                  const char* setup = "standard") {
  auto game =
      GameState::start({"alice", "bob"}, variant, Opening{fen, white_seat}, kClock, kT0, setup);
  EXPECT_TRUE(game.ok()) << game.status();
  return *game;
}

GameState Played(GameState game, const std::vector<std::string>& moves) {
  for (const std::string& uci : moves) {
    auto next = game.move(game.whoseTurn(), uci, kT0);
    EXPECT_TRUE(next.ok()) << uci << ": " << next.status();
    game = *std::move(next);
  }
  return game;
}

GameState ScholarsMate(int white_seat = 0) {
  return Played(Started(white_seat), {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"});
}

PgnTags Tags() { return PgnTags{"Room ABCDEF", "muchq.com", "GHJKLM.1", kEndedAt}; }

TEST(ChessPgn, ReplayNamesEveryMoveInSanAndEveryPositionFromTheStart) {
  const GameState game = ScholarsMate();
  const Replayed replayed = ReplayGame(game);
  EXPECT_EQ(replayed.san,
            (std::vector<std::string>{"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7#"}));
  ASSERT_EQ(replayed.fens.size(), 8u);
  EXPECT_EQ(replayed.fens.front(), kStandard);
  EXPECT_EQ(replayed.fens.back(), game.fen());
}

TEST(ChessPgn, PgnOfAStandardGame) {
  const std::string pgn = ToPgn(ScholarsMate(), Tags());
  EXPECT_EQ(pgn,
            "[Event \"Room ABCDEF\"]\n"
            "[Site \"muchq.com\"]\n"
            "[Date \"2026.10.07\"]\n"
            "[Round \"GHJKLM.1\"]\n"
            "[White \"alice\"]\n"
            "[Black \"bob\"]\n"
            "[Result \"1-0\"]\n"
            "[UTCDate \"2026.10.07\"]\n"
            "[UTCTime \"12:00:00\"]\n"
            "[TimeControl \"180+2\"]\n"
            "[Termination \"normal\"]\n"
            "\n"
            "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n");
}

// The archive keeps a finished game as its stored row, and exports it from
// what that row restores to.
TEST(ChessPgn, ARestoredGameExportsAsTheGameItWas) {
  const GameState game = ScholarsMate(1);
  const auto restored = deserializeGameState(serializeGameState(game));
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(ToPgn(*restored, Tags()), ToPgn(game, Tags()));
}

TEST(ChessPgn, PgnOfAGameInPlayHasNoResult) {
  const std::string pgn = ToPgn(Played(Started(), {"e2e4"}), Tags());
  EXPECT_NE(pgn.find("[Result \"*\"]"), std::string::npos) << pgn;
  EXPECT_EQ(pgn.find("[Termination"), std::string::npos) << pgn;
  EXPECT_NE(pgn.find("\n1. e4 *\n"), std::string::npos) << pgn;
}

TEST(ChessPgn, PgnOfASetupNamesItsPositionAndCountsFromIt) {
  // Black to move first: the movetext opens on "1...".
  auto game = Played(Started(0, "7k/8/6K1/8/8/8/4p3/8 b - - 0 1", "kpk", "kpk-e2"), {"h8g8"});
  game = *game.resign(game.whoseTurn(), kT0);
  const std::string pgn = ToPgn(game, Tags());
  EXPECT_NE(pgn.find("[SetUp \"1\"]\n[FEN \"7k/8/6K1/8/8/8/4p3/8 b - - 0 1\"]\n"),
            std::string::npos)
      << pgn;
  EXPECT_NE(pgn.find("\n1... Kg8 0-1\n"), std::string::npos) << pgn;
}

TEST(ChessPgn, PgnSpellsHowTheGameEnded) {
  const GameState flagged = *Started().flag(kT0 + 180'000);
  const std::string timeout = ToPgn(flagged, Tags());
  EXPECT_NE(timeout.find("[Result \"0-1\"]"), std::string::npos) << timeout;
  EXPECT_NE(timeout.find("[Termination \"time forfeit\"]"), std::string::npos);
  EXPECT_NE(timeout.find("\n0-1\n"), std::string::npos) << "no moves, only the result";

  const GameState abandoned = *Started().removePlayer(1, kT0);
  const std::string left = ToPgn(abandoned, Tags());
  EXPECT_NE(left.find("[Termination \"abandoned\"]"), std::string::npos) << left;
}

TEST(ChessPgn, PgnSpellsADraw) {
  // Kxf5 takes the last rook: kings alone.
  const GameState game =
      Played(Started(0, "7k/8/6K1/5r2/8/8/8/8 w - - 0 1", "rpr", "rpr-lucena"), {"g6f5"});
  ASSERT_TRUE(game.isOver());
  const std::string pgn = ToPgn(game, Tags());
  EXPECT_NE(pgn.find("[Result \"1/2-1/2\"]"), std::string::npos) << pgn;
  EXPECT_NE(pgn.find("1. Kxf5 1/2-1/2\n"), std::string::npos) << pgn;
}

TEST(ChessPgn, PgnEscapesTagValues) {
  PgnTags tags = Tags();
  tags.event = R"(a "quoted" \ room)";
  const std::string pgn = ToPgn(ScholarsMate(), tags);
  EXPECT_NE(pgn.find(R"([Event "a \"quoted\" \\ room"])"), std::string::npos) << pgn;
}

TEST(ChessPgn, LongMovetextWrapsUnderEightyColumns) {
  std::mt19937 gen(1633);  // fixed: the same games every run
  GameState game = Started();
  while (!game.isOver() && game.moves().size() < 60) {
    const auto& legal = game.legalMoves();
    game = *game.move(game.whoseTurn(), legal[gen() % legal.size()], kT0);
  }
  const std::string pgn = ToPgn(game, Tags());
  const std::string movetext = pgn.substr(pgn.find("\n\n") + 2);
  std::size_t start = 0;
  int lines = 0;
  for (std::size_t end = movetext.find('\n'); end != std::string::npos;
       start = end + 1, end = movetext.find('\n', start)) {
    EXPECT_LE(end - start, 79u) << movetext;
    ++lines;
  }
  EXPECT_GT(lines, 1) << movetext;
}

// What one_d4's worker reads is chess_cpp's parser, so the export is
// proven the way it will be consumed: every random game parses back to
// one game, replays from the tagged start, and lands on the hub's final
// position.
TEST(ChessPgn, RandomGamesRoundTripThroughTheIndexersParser) {
  std::mt19937 gen(1633);  // fixed: the same games every run
  int plies = 0;
  for (int i = 0; i < 40; ++i) {
    const bool kpk = i % 4 == 3;
    GameState game =
        kpk ? Started(0, "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", "kpk", "kpk-e2") : Started(i % 2);
    while (!game.isOver() && game.moves().size() < 150) {
      const auto& legal = game.legalMoves();
      const std::string uci = legal[gen() % legal.size()];
      game = *game.move(game.whoseTurn(), uci, kT0);
    }
    if (!game.isOver()) game = *game.resign(0, kT0);
    plies += static_cast<int>(game.moves().size());

    const std::string pgn = ToPgn(game, Tags());
    const auto parsed = chess_cpp::ParseGame(pgn);
    ASSERT_TRUE(parsed.ok()) << parsed.status() << "\n" << pgn;
    ASSERT_EQ(parsed->san_moves.size(), game.moves().size()) << pgn;
    const auto start = chess_cpp::StartFen(parsed->headers);
    ASSERT_TRUE(start.ok()) << start.status();
    std::string last;
    const absl::Status replayed = chess_cpp::ReplayFrom(
        *start, parsed->san_moves,
        [&](const chess_cpp::Position& position) { last = position.board.getFen(); });
    ASSERT_TRUE(replayed.ok()) << replayed << "\n" << pgn;
    EXPECT_EQ(last, game.fen()) << pgn;
  }
  EXPECT_GT(plies, 40 * 20);
}

// The moves a random game is least likely to play, written out: en
// passant, both castles and a promotion, through the same parser.
TEST(ChessPgn, SpecialMovesRoundTripThroughTheIndexersParser) {
  const GameState game =
      Played(Started(0, "r3k2r/1P6/8/3pP3/8/8/8/R3K2R w KQkq d6 0 1", "standard", "standard"),
             {"e5d6", "e8g8", "e1c1", "g8g7", "b7b8q"});
  EXPECT_EQ(ReplayGame(game).san,
            (std::vector<std::string>{"exd6", "O-O", "O-O-O", "Kg7", "b8=Q"}));
  const std::string pgn = ToPgn(game, Tags());
  const auto parsed = chess_cpp::ParseGame(pgn);
  ASSERT_TRUE(parsed.ok()) << parsed.status() << "\n" << pgn;
  const auto start = chess_cpp::StartFen(parsed->headers);
  ASSERT_TRUE(start.ok()) << start.status();
  std::string last;
  ASSERT_TRUE(chess_cpp::ReplayFrom(*start, parsed->san_moves,
                                    [&](const chess_cpp::Position& p) { last = p.board.getFen(); })
                  .ok())
      << pgn;
  EXPECT_EQ(last, game.fen()) << pgn;
}

}  // namespace
}  // namespace chess_play
