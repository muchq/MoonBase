// The race against the word lists the hub ships: mithril's dictionary for
// every word played, and the common words puzzles start and end on. Every
// letter count a race offers has puzzles, and none starts or ends on a
// blocklisted word.
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

TEST(Words, EveryLetterCountHasPuzzles) {
  for (int letters = kMinLetters; letters <= kMaxLetters; ++letters) {
    EXPECT_GT(Words().wordsOfLength(letters).size(), 1000U) << letters;
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

TEST(Words, NoBlocklistedWordIsCommon) {
  const Dictionary blocked = Dictionary::FromLines(Read("BLOCKLIST"));
  int checked = 0;
  for (int letters = kMinLetters; letters <= kMaxLetters; ++letters) {
    for (const std::string& word : blocked.wordsOfLength(letters)) {
      EXPECT_FALSE(Common().contains(word)) << word;
      ++checked;
    }
  }
  EXPECT_GT(checked, 20);
}

}  // namespace
}  // namespace wordchain_race
