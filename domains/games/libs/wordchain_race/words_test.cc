// The race against the word list the hub ships, mithril's: every letter
// count a race offers has puzzles to pick, and a picked puzzle's start and
// target are words a runner can play from.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "domains/games/libs/wordchain_race/dictionary.h"

namespace wordchain_race {
namespace {

const Dictionary& Words() {
  static const Dictionary d = [] {
    const char* path = std::getenv("WORDS");
    std::ifstream in(path == nullptr ? "" : path);
    std::stringstream text;
    text << in.rdbuf();
    return Dictionary::FromLines(text.str());
  }();
  return d;
}

TEST(Words, EveryLetterCountHasPuzzles) {
  for (int letters = kMinLetters; letters <= kMaxLetters; ++letters) {
    EXPECT_GT(Words().wordsOfLength(letters).size(), 1000U) << letters;
    for (uint64_t seed = 0; seed < 10; ++seed) {
      auto puzzle = PickPuzzle(Words(), Words(), letters, seed);
      ASSERT_TRUE(puzzle.ok()) << letters << "/" << seed << ": " << puzzle.status();
      EXPECT_TRUE(Words().contains(puzzle->start));
      EXPECT_TRUE(Words().contains(puzzle->target));
      EXPECT_EQ(static_cast<int>(puzzle->start.size()), letters);
    }
  }
}

}  // namespace
}  // namespace wordchain_race
