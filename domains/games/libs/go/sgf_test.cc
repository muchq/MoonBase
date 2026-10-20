#include "domains/games/libs/go/sgf.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace go {
namespace {

constexpr int64_t kT0 = 1'800'000'000'000;
// 2026-10-20, noon UTC.
constexpr int64_t kEnded = 1'792'497'600'000;
constexpr TimeControl kMinute{60'000, 0};

GameState Start(std::vector<std::string> players = {"ann", "bo"}, double komi = kDefaultKomi) {
  return *GameState::start(std::move(players), 9, komi, 0, kMinute, kT0);
}

GameState Played(GameState state, const std::vector<std::pair<int, int>>& points) {
  for (const auto& [x, y] : points) {
    auto next = state.play(state.whoseTurn(), x, y, kT0);
    EXPECT_TRUE(next.ok()) << "(" << x << "," << y << "): " << next.status();
    if (next.ok()) state = *next;
  }
  return state;
}

GameState Scored(GameState state) {
  state = *state.pass(state.whoseTurn(), kT0);
  state = *state.pass(state.whoseTurn(), kT0);
  return *(*state.accept(0, kT0)).accept(1, kT0);
}

TEST(Sgf, WritesTheHeaderThenEachMoveAPassEmpty) {
  const GameState ended = Scored(Played(Start(), {{2, 2}, {6, 6}}));
  EXPECT_EQ(ToSgf(ended, kEnded),
            "(;FF[4]GM[1]CA[UTF-8]SZ[9]KM[6.5]PB[ann]PW[bo]DT[2026-10-20]RE[W+6.5]"
            ";B[cc];W[gg];B[];W[])");
}

TEST(Sgf, AGameInPlayHasNoResult) {
  EXPECT_EQ(ToSgf(Played(Start(), {{0, 8}}), kEnded),
            "(;FF[4]GM[1]CA[UTF-8]SZ[9]KM[6.5]PB[ann]PW[bo]DT[2026-10-20];B[ai])");
}

TEST(Sgf, EachEndingIsSpelledAsSgfSpellsIt) {
  const GameState open = Start();
  const auto result_of = [](const GameState& game) {
    const std::string sgf = ToSgf(game, kEnded);
    const std::size_t at = sgf.find("RE[");
    return at == std::string::npos ? std::string() : sgf.substr(at + 3, sgf.find(']', at) - at - 3);
  };
  EXPECT_EQ(result_of(*open.resign(1, kT0)), "B+R");
  EXPECT_EQ(result_of(*open.flag(kT0 + kMinute.initial_ms)), "W+T");
  EXPECT_EQ(result_of(*open.removePlayer(0, kT0)), "W+F");
  EXPECT_EQ(result_of(Scored(Start({"ann", "bo"}, 0))), "0") << "an even score";
  EXPECT_EQ(result_of(Scored(Played(Start(), {{4, 4}}))), "B+74.5");
}

TEST(Sgf, EscapesWhatWouldCloseOrQuoteAName) {
  const std::string sgf = ToSgf(Start({"a]b", "c\\d"}), kEnded);
  EXPECT_NE(sgf.find("PB[a\\]b]"), std::string::npos) << sgf;
  EXPECT_NE(sgf.find("PW[c\\\\d]"), std::string::npos) << sgf;
}

// GNU Go reads the record onto the board play reached, captures included.
TEST(Sgf, GnuGoLoadsTheGameOntoTheSameBoard) {
  const char* gnugo = std::getenv("GNUGO");
  ASSERT_NE(gnugo, nullptr);
  const GameState game = Played(Start(), {{1, 0}, {0, 0}, {0, 1}, {5, 5}, {3, 3}});
  ASSERT_EQ(game.at(0, 0), kEmpty) << "the corner stone is captured";

  const std::string dir = std::getenv("TEST_TMPDIR");
  const std::string sgf_path = dir + "/game.sgf";
  const std::string commands_path = dir + "/commands";
  std::ofstream(sgf_path) << ToSgf(game, kEnded);
  std::ofstream(commands_path) << "loadsgf " << sgf_path
                               << "\nlist_stones black\nlist_stones white\nget_komi\nquit\n";
  FILE* out = popen((std::string(gnugo) + " --mode gtp < " + commands_path).c_str(), "r");
  ASSERT_NE(out, nullptr);
  std::string reply;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), out) != nullptr) reply += buffer;
  ASSERT_EQ(pclose(out), 0) << reply;

  // Each answer is "= ...", then a blank line.
  std::vector<std::string> answers;
  std::istringstream lines(reply);
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind("=", 0) != 0) continue;
    const std::size_t text = line.find_first_not_of(" =");
    answers.push_back(text == std::string::npos ? "" : line.substr(text));
  }
  ASSERT_EQ(answers.size(), 5u) << reply;
  const auto stones = [&](char stone) {
    std::set<std::string> vertices;
    constexpr std::string_view kColumns = "ABCDEFGHJ";
    for (int y = 0; y < 9; ++y) {
      for (int x = 0; x < 9; ++x) {
        if (game.at(x, y) == stone)
          vertices.insert(std::string(1, kColumns[x]) + std::to_string(9 - y));
      }
    }
    return vertices;
  };
  const auto listed = [](const std::string& answer) {
    std::set<std::string> vertices;
    std::istringstream words(answer);
    for (std::string word; words >> word;) vertices.insert(word);
    return vertices;
  };
  EXPECT_EQ(listed(answers[1]), stones(kBlackStone)) << reply;
  EXPECT_EQ(listed(answers[2]), stones(kWhiteStone)) << reply;
  EXPECT_EQ(answers[3], "6.5") << reply;
}

}  // namespace
}  // namespace go
