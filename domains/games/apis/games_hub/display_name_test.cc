#include "domains/games/apis/games_hub/display_name.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace games_hub {
namespace {

// The kept name, or "<refused: why>".
std::string Kept(std::string_view name) {
  auto kept = NormalizeDisplayName(name);
  return kept.ok() ? *kept : "<refused: " + std::string(kept.status().message()) + ">";
}

TEST(DisplayName, IsTrimmed) {
  EXPECT_EQ(Kept("  Ann Lee \t"), "Ann Lee");
  EXPECT_EQ(Kept("Zoë"), "Zoë");
}

TEST(DisplayName, EmptyClearsIt) {
  EXPECT_EQ(Kept(""), "");
  EXPECT_EQ(Kept("   "), "");
}

// Counted in characters, not bytes: a name of accented letters fits as
// its ASCII twin does.
TEST(DisplayName, HoldsAtMostTwentyFourCharacters) {
  EXPECT_EQ(Kept(std::string(24, 'a')), std::string(24, 'a'));
  EXPECT_FALSE(NormalizeDisplayName(std::string(25, 'a')).ok());
  std::string accented;
  for (int i = 0; i < 24; ++i) accented += "é";
  EXPECT_EQ(Kept(accented), accented);
  EXPECT_FALSE(NormalizeDisplayName(accented + "é").ok());
}

TEST(DisplayName, SaysWhyItRefuses) {
  EXPECT_EQ(Kept(std::string(25, 'a')), "<refused: a name is at most 24 characters>");
  EXPECT_EQ(Kept("Ann\nLee"), "<refused: a name is printable text>");
  EXPECT_EQ(Kept("mithril"), "<refused: that name is a bot's>");
}

TEST(DisplayName, RefusesControlCharactersAndBrokenText) {
  EXPECT_FALSE(NormalizeDisplayName("Ann\nLee").ok());
  EXPECT_FALSE(NormalizeDisplayName(std::string("Ann\0Lee", 7)).ok());
  EXPECT_FALSE(NormalizeDisplayName("\xff\xfe").ok());
  EXPECT_FALSE(NormalizeDisplayName("Ann\x7f").ok());
}

// A chat bot's name would put words in its mouth.
TEST(DisplayName, RefusesABotsNameInAnyCase) {
  EXPECT_FALSE(NormalizeDisplayName("microgpt").ok());
  EXPECT_FALSE(NormalizeDisplayName("MiThRiL").ok());
  EXPECT_EQ(Kept("microgpt fan"), "microgpt fan");
}

}  // namespace
}  // namespace games_hub
