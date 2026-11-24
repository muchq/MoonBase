// The race against the word lists the hub ships: race_words for every word
// played, and the common words puzzles start and end on. Every letter count a
// race offers has puzzles.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "domains/games/libs/wordchain_race/dictionary.h"

namespace wordchain_race {
namespace {

std::string Read(const char* env) {
  const char* path = std::getenv(env);
  std::ifstream in(path == nullptr ? "" : path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

const Dictionary& Words() {
  static const Dictionary d = Dictionary::FromLines(Read("WORDS"));
  return d;
}

const Dictionary& Common() {
  static const Dictionary d = Dictionary::FromLines(Read("COMMON"));
  return d;
}

// The lists' sizes, pinned: a regenerated or edited list changes them on
// purpose, in this test, not by accident.
TEST(Words, ListsHoldTheirReviewedSizes) {
  std::size_t words = 0;
  std::size_t common = 0;
  for (int letters = kMinLetters; letters <= kMaxLetters; ++letters) {
    words += Words().wordsOfLength(letters).size();
    common += Common().wordsOfLength(letters).size();
  }
  EXPECT_EQ(words, 13511U);
  EXPECT_EQ(common, 5786U);
}

TEST(Words, EveryLetterCountHasPuzzles) {
  for (int letters = kMinLetters; letters <= kMaxLetters; ++letters) {
    EXPECT_GT(Words().wordsOfLength(letters).size(), 500U) << letters;
    EXPECT_GT(Common().wordsOfLength(letters).size(), 300U) << letters;
    for (uint64_t seed = 0; seed < 10; ++seed) {
      auto puzzle = PickPuzzle(Words(), Common(), letters, seed);
      ASSERT_TRUE(puzzle.ok()) << letters << "/" << seed << ": " << puzzle.status();
      EXPECT_TRUE(Common().contains(puzzle->start));
      EXPECT_TRUE(Common().contains(puzzle->target));
      EXPECT_EQ(static_cast<int>(puzzle->start.size()), letters);
    }
  }
}

TEST(Words, PlaysInflectedForms) {
  for (const char* word : {"bugs", "asked", "aces", "aimed", "cats"}) {
    EXPECT_TRUE(Words().contains(word)) << word;
  }
}

}  // namespace
}  // namespace wordchain_race
