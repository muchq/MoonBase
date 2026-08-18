#include "domains/games/apis/one_d4_worker/result.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace one_d4_worker {
namespace {

TEST(GameResult, ReadsAnExplicitWin) {
  EXPECT_EQ(ResultOf("win", "resigned"), "1-0");
  EXPECT_EQ(ResultOf("checkmated", "win"), "0-1");
}

TEST(GameResult, ReadsADrawFromEitherSide) {
  EXPECT_EQ(ResultOf("agreed", "agreed"), "1/2-1/2");
  EXPECT_EQ(ResultOf("repetition", "repetition"), "1/2-1/2");
  EXPECT_EQ(ResultOf("stalemate", "stalemate"), "1/2-1/2");
  EXPECT_EQ(ResultOf("insufficient", "insufficient"), "1/2-1/2");
  EXPECT_EQ(ResultOf("50move", "50move"), "1/2-1/2");
  EXPECT_EQ(ResultOf("timevsinsufficient", "timevsinsufficient"), "1/2-1/2");
  EXPECT_EQ(ResultOf("drawn", "drawn"), "1/2-1/2");
}

TEST(GameResult, InfersTheWinnerFromTheLoser) {
  // chess.com says why the loser lost and only "win" for the winner, but a
  // game can arrive with the loss recorded and the win missing.
  EXPECT_EQ(ResultOf("resigned", ""), "0-1");
  EXPECT_EQ(ResultOf("", "timeout"), "1-0");
  EXPECT_EQ(ResultOf("abandoned", ""), "0-1");
  EXPECT_EQ(ResultOf("", "checkmated"), "1-0");
  EXPECT_EQ(ResultOf("lose", ""), "0-1");
}

TEST(GameResult, SaysUnknownRatherThanGuessing) {
  EXPECT_EQ(ResultOf("", ""), "unknown");
  EXPECT_EQ(ResultOf("something new", "something newer"), "unknown");
}

// --- the vocabulary is chess.com's, and both indexers have to agree ------

std::set<std::string> WordsIn(const std::string& source, const std::string& method) {
  // The declaration, not the call site: mapResult names both methods
  // before either is defined, and starting there reads one vocabulary as
  // the other.
  const auto start = source.find("boolean " + method + "(");
  EXPECT_NE(start, std::string::npos) << method << " is gone from ResultMapper";
  if (start == std::string::npos) return {};
  const auto end = source.find("\n  }", start);
  const std::string body = source.substr(start, end - start);

  std::set<std::string> words;
  const std::regex quoted(R"re("([a-z0-9]+)")re");
  for (std::sregex_iterator it(body.begin(), body.end(), quoted), end; it != end; ++it) {
    words.insert((*it)[1]);
  }
  return words;
}

std::string JavaSource() {
  std::ifstream file(
      "domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/ResultMapper.java");
  EXPECT_TRUE(file.good()) << "ResultMapper.java is not where this test looks";
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

TEST(GameResult, DrawsAreTheSameWordsTheJavaWorkerKnows) {
  // Both workers write `result` for the same games. A word one of them
  // learns and the other does not is two indexes that disagree about who
  // won, on rows a query cannot tell apart.
  for (const std::string& word : WordsIn(JavaSource(), "isDrawResult")) {
    EXPECT_EQ(ResultOf(word, word), "1/2-1/2") << word << " is a draw in Java and not here";
  }
}

TEST(GameResult, LossesAreTheSameWordsToo) {
  for (const std::string& word : WordsIn(JavaSource(), "isLossResult")) {
    EXPECT_EQ(ResultOf(word, ""), "0-1") << word << " is a loss in Java and not here";
    EXPECT_EQ(ResultOf("", word), "1-0") << word << " is a loss in Java and not here";
  }
}

TEST(GameResult, KnowsNoWordsTheJavaWorkerDoesNot) {
  // The other direction, and the one the tests above cannot see: a word
  // added here and not there is the same two-indexes-disagree harm, with
  // this worker calling a game drawn that the other calls unfinished.
  const std::set<std::string> draws = WordsIn(JavaSource(), "isDrawResult");
  const std::set<std::string> losses = WordsIn(JavaSource(), "isLossResult");
  ASSERT_FALSE(draws.empty());
  ASSERT_FALSE(losses.empty());

  for (const std::string_view word : KnownDraws()) {
    EXPECT_TRUE(draws.count(std::string(word)) != 0) << word << " is a draw here and not in Java";
  }
  for (const std::string_view word : KnownLosses()) {
    EXPECT_TRUE(losses.count(std::string(word)) != 0) << word << " is a loss here and not in Java";
  }
}

TEST(GameResult, TheContractTestReadsSomethingRatherThanNothing) {
  // The regex above quietly returning an empty set would make both tests
  // above pass while checking nothing at all.
  EXPECT_GE(WordsIn(JavaSource(), "isDrawResult").size(), 7u);
  EXPECT_GE(WordsIn(JavaSource(), "isLossResult").size(), 5u);
}

}  // namespace
}  // namespace one_d4_worker
