#include "domains/games/libs/wordchain_race/dictionary.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace wordchain_race {
namespace {

using Ladder = std::vector<std::string>;

// cold → cord → card → ward → warm, and a dead end.
constexpr std::string_view kWords =
    "cold\ncord\ncard\nward\nwarm\nworm\nfizz\nCord\nab\nabcdef\nca-t\nCOLD\n";

TEST(OneLetterApart, SameLengthOneDifference) {
  EXPECT_TRUE(OneLetterApart("cold", "cord"));
  EXPECT_FALSE(OneLetterApart("cold", "cold"));
  EXPECT_FALSE(OneLetterApart("cold", "card"));
  EXPECT_FALSE(OneLetterApart("cold", "colds"));
}

TEST(Dictionary, KeepsOnlyLowercaseWordsOfRaceLength) {
  Dictionary d = Dictionary::FromLines(kWords);
  EXPECT_TRUE(d.contains("cold"));
  EXPECT_FALSE(d.contains("Cord"));
  EXPECT_FALSE(d.contains("COLD"));
  EXPECT_FALSE(d.contains("ab"));
  EXPECT_FALSE(d.contains("abcdef"));
  EXPECT_FALSE(d.contains("ca-t"));
  EXPECT_EQ(d.wordsOfLength(4), (Ladder{"card", "cold", "cord", "fizz", "ward", "warm", "worm"}));
  EXPECT_TRUE(d.wordsOfLength(2).empty());
}

TEST(Dictionary, ReadsCrlfLines) {
  Dictionary d = Dictionary::FromLines("cold\r\ncord\r\n");
  EXPECT_TRUE(d.contains("cold"));
  EXPECT_TRUE(d.contains("cord"));
}

TEST(Dictionary, ShortestPathJoinsBothEnds) {
  Dictionary d = Dictionary::FromLines(kWords);
  EXPECT_EQ(d.shortestPath("cold", "warm"), (Ladder{"cold", "cord", "card", "ward", "warm"}));
  EXPECT_EQ(d.shortestPath("cold", "cold"), (Ladder{"cold"}));
}

TEST(Dictionary, ShortestPathNothingWhenUnjoinedOrUnknown) {
  Dictionary d = Dictionary::FromLines(kWords);
  EXPECT_EQ(d.shortestPath("cold", "fizz"), std::nullopt);
  EXPECT_EQ(d.shortestPath("cold", "zzzz"), std::nullopt);
  EXPECT_EQ(d.shortestPath("cold", "cat"), std::nullopt);
}

// A chain a-b-c-d-e-f-g-h of 3-letter words, one letter apart each step,
// so every pair's distance is known.
constexpr std::string_view kChain = "aaa\nbaa\nbba\nbbb\ncbb\nccb\nccc\ndcc\n";

TEST(PickPuzzle, OptimalLadderWithinBounds) {
  Dictionary d = Dictionary::FromLines(kChain);
  for (uint64_t seed = 0; seed < 50; ++seed) {
    auto puzzle = PickPuzzle(d, d, 3, seed);
    ASSERT_TRUE(puzzle.ok()) << seed << ": " << puzzle.status();
    auto path = d.shortestPath(puzzle->start, puzzle->target);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(static_cast<int>(path->size()), puzzle->optimal);
    // The ladder it was picked on: this word list's only one.
    EXPECT_EQ(puzzle->ladder, *path) << seed;
    EXPECT_GE(puzzle->optimal, kMinOptimal);
    EXPECT_LE(puzzle->optimal, kMaxOptimal);
  }
}

TEST(PickPuzzle, SameSeedSamePuzzleAndSeedsVary) {
  Dictionary d = Dictionary::FromLines(kChain);
  EXPECT_EQ(*PickPuzzle(d, d, 3, 7), *PickPuzzle(d, d, 3, 7));
  std::set<std::pair<std::string, std::string>> seen;
  for (uint64_t seed = 0; seed < 50; ++seed) {
    auto puzzle = *PickPuzzle(d, d, 3, seed);
    seen.emplace(puzzle.start, puzzle.target);
  }
  EXPECT_GT(seen.size(), 5U);
}

TEST(PickPuzzle, DrawsEndsFromTheEndpointsTheDictionaryHolds) {
  Dictionary d = Dictionary::FromLines(kChain);
  // zzz is no word of the dictionary's, so only aaa and ccc qualify.
  Dictionary ends = Dictionary::FromLines("aaa\nccc\nzzz\n");
  for (uint64_t seed = 0; seed < 20; ++seed) {
    auto puzzle = PickPuzzle(d, ends, 3, seed);
    ASSERT_TRUE(puzzle.ok()) << puzzle.status();
    EXPECT_TRUE((puzzle->start == "aaa" && puzzle->target == "ccc") ||
                (puzzle->start == "ccc" && puzzle->target == "aaa"));
    EXPECT_EQ(puzzle->optimal, 7);
  }
}

TEST(Dictionary, ShortestPathGivesUpPastMaxWords) {
  Dictionary d = Dictionary::FromLines(kChain);
  EXPECT_EQ(d.shortestPath("aaa", "ccc", 7)->size(), 7U);
  EXPECT_EQ(d.shortestPath("aaa", "ccc", 6), std::nullopt);
}

TEST(PickPuzzle, RefusesLettersOutOfRange) {
  Dictionary d = Dictionary::FromLines(kChain);
  EXPECT_FALSE(PickPuzzle(d, d, 2, 0).ok());
  EXPECT_FALSE(PickPuzzle(d, d, 6, 0).ok());
}

TEST(PickPuzzle, RefusesWhenNoPairQualifies) {
  // Every ladder here is too short to race.
  Dictionary d = Dictionary::FromLines("cold\ncord\ncard\n");
  EXPECT_FALSE(PickPuzzle(d, d, 4, 0).ok());
  EXPECT_FALSE(PickPuzzle(d, d, 5, 0).ok());
}

}  // namespace
}  // namespace wordchain_race
