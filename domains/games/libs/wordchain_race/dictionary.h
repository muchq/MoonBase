#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_DICTIONARY_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_DICTIONARY_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "absl/status/statusor.h"

namespace wordchain_race {

/// The shortest and longest words a race is played in.
inline constexpr int kMinLetters = 3;
inline constexpr int kMaxLetters = 5;

/// Whether `a` and `b` are the same length and differ in exactly one place.
[[nodiscard]] bool OneLetterApart(std::string_view a, std::string_view b);

/// The words a race accepts: every line of a word list that is kMinLetters
/// to kMaxLetters lowercase ASCII letters. Capitalized words (proper nouns)
/// and anything else are left out.
class Dictionary {
 public:
  [[nodiscard]] static Dictionary FromLines(std::string_view text);

  [[nodiscard]] bool contains(std::string_view word) const;
  /// The words of `letters` letters, sorted; empty outside the race's range.
  [[nodiscard]] const std::vector<std::string>& wordsOfLength(int letters) const;
  /// A shortest ladder from `start` to `end`, both included, each word one
  /// letter from the last; nothing when either is unknown, none joins them,
  /// or every one is longer than `max_words`.
  [[nodiscard]] std::optional<std::vector<std::string>> shortestPath(std::string_view start,
                                                                     std::string_view end,
                                                                     int max_words = 0) const;

 private:
  std::unordered_set<std::string> words_;
  std::map<int, std::vector<std::string>> by_length_;
};

/// A race's puzzle: climb from `start` to `target`. `optimal` counts the
/// words in a shortest ladder, both ends included.
struct Puzzle {
  std::string start;
  std::string target;
  int optimal = 0;
  bool operator==(const Puzzle&) const = default;
};

/// The shortest and longest optimal ladders a picked puzzle has: long
/// enough to race, short enough to finish.
inline constexpr int kMinOptimal = 4;
inline constexpr int kMaxOptimal = 7;

/// A puzzle of `letters`-letter words whose shortest ladder through
/// `dictionary` is kMinOptimal to kMaxOptimal words. Its start and target
/// are drawn from `endpoints` (common words, so a race starts and ends
/// somewhere a player recognizes) and must be in `dictionary` too. The same
/// seed and lists pick the same puzzle. Refused when `letters` is out of
/// range or no such pair turns up.
[[nodiscard]] absl::StatusOr<Puzzle> PickPuzzle(const Dictionary& dictionary,
                                                const Dictionary& endpoints, int letters,
                                                uint64_t seed);

}  // namespace wordchain_race

#endif  // DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_DICTIONARY_H
