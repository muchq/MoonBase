#include "domains/games/libs/chess_cpp/parsed_game.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace chess_cpp {
namespace {

TEST(Headers, KeepsTagsInTheOrderTheyWereAdded) {
  Headers headers;
  headers.Add("Event", "Live Chess");
  headers.Add("Site", "Chess.com");

  EXPECT_EQ(headers.size(), 2u);
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(headers.entries().front().first, "Event");
  EXPECT_EQ(headers.entries().back().first, "Site");
}

TEST(Headers, IsEmptyBeforeAnythingIsAdded) {
  const Headers headers;
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(headers.Get("Event"), std::nullopt);
}

TEST(Headers, LooksUpTagNamesExactly) {
  Headers headers;
  headers.Add("ECO", "C55");

  EXPECT_EQ(headers.Get("ECO"), "C55");
  EXPECT_EQ(headers.Get("eco"), std::nullopt) << "PGN tag names are case-sensitive";
  EXPECT_EQ(headers.Get("EC"), std::nullopt) << "and are not prefixes";
}

TEST(Headers, KeepsTheFirstValueOfARepeatedTagAndStillShowsTheRest) {
  // Malformed input rather than something to resolve by guessing: the
  // lookup answers with the first, and entries() keeps enough to see that
  // the file was odd.
  Headers headers;
  headers.Add("Result", "1-0");
  headers.Add("Result", "0-1");

  EXPECT_EQ(headers.Get("Result"), "1-0");
  EXPECT_EQ(headers.size(), 2u);
  EXPECT_EQ(headers.entries().back().second, "0-1");
}

}  // namespace
}  // namespace chess_cpp
