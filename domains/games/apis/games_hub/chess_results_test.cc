#include "domains/games/apis/games_hub/chess_results.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"

namespace games_hub {
namespace {

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";

chess_play::Table Mated(const chess_play::Table& table) {
  const int white = table.game().whiteSeat();
  return *table.inGame(
      [&](const chess_play::GameState& game) { return game.move(white, "e7e8q", 0); });
}

TEST(ChessResults, AWinIsItsPlayerAndColorADrawNeither) {
  const auto won = WireChessResult({std::string("bob"), chess_play::Ending::kResignation,
                                    chess_play::Color::kBlack});
  EXPECT_EQ(won.winner, std::optional<std::string>("bob"));
  EXPECT_EQ(won.winnerColor, std::optional<std::string>("black"));
  EXPECT_EQ(won.ending, "resignation");
  const auto drawn =
      WireChessResult({std::nullopt, chess_play::Ending::kStalemate, std::nullopt});
  EXPECT_FALSE(drawn.winner.has_value());
  EXPECT_FALSE(drawn.winnerColor.has_value());
  EXPECT_EQ(drawn.ending, "stalemate");
}

// An instance that hears of a table only once its next game is under way
// still owes the game before it: the owed results come off the sheet, not
// the game in play.
TEST(ChessResults, AGameEndedBeforeTheNextBeganIsStillOwed) {
  const auto opened =
      chess_play::Table::open({"alice", "bob"}, "kpk", {kMate, 0}, {60'000, 0}, 0);
  ASSERT_TRUE(opened.ok()) << opened.status();
  const auto next = Mated(*opened).next("kpk", {kMate, 0}, {60'000, 0}, 1'000);
  ASSERT_TRUE(next.ok()) << next.status();
  ASSERT_FALSE(next->game().isOver());

  const auto owed = ChessResultsSince(*next, 0);
  ASSERT_EQ(owed.size(), 1u);
  EXPECT_EQ(owed[0].winner, std::optional<std::string>("alice"));
  EXPECT_EQ(owed[0].winnerColor, std::optional<std::string>("white"));
  EXPECT_EQ(owed[0].ending, "checkmate");
  EXPECT_TRUE(ChessResultsSince(*next, 1).empty());

  // Two told of one: nothing, never a wrap.
  EXPECT_TRUE(ChessResultsSince(*next, 2).empty());
  // The second game's end owes only itself.
  const auto both = ChessResultsSince(Mated(*next), 1);
  ASSERT_EQ(both.size(), 1u);
  EXPECT_EQ(both[0].winner, std::optional<std::string>("bob"));
}

}  // namespace
}  // namespace games_hub
