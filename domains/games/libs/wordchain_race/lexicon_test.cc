#include "domains/games/libs/wordchain_race/lexicon.h"

#include <gtest/gtest.h>

namespace wordchain_race {
namespace {

// The embedded lists are the generated ones: words_test checks their
// contents; this checks they arrive whole.
TEST(EnableLexicon, HoldsBothListsWhole) {
  const Lexicon& lexicon = EnableLexicon();
  EXPECT_EQ(lexicon.name, "enable");
  EXPECT_TRUE(lexicon.words.contains("aah"));
  EXPECT_TRUE(lexicon.words.contains("zymes"));
  EXPECT_GT(lexicon.words.wordsOfLength(4).size(), 3000U);
  EXPECT_TRUE(lexicon.endpoints.contains("cold"));
  EXPECT_GT(lexicon.endpoints.wordsOfLength(5).size(), 3000U);
  EXPECT_TRUE(PickPuzzle(lexicon.words, lexicon.endpoints, 4, 1).ok());
}

TEST(EnableLexicon, CarriesScowlsCopyright) {
  EXPECT_NE(ScowlCopyright().find("Kevin Atkinson"), std::string_view::npos);
}

}  // namespace
}  // namespace wordchain_race
