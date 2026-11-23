#include "domains/games/libs/wordchain_race/dictionary.h"

#include <algorithm>
#include <deque>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace wordchain_race {
namespace {

bool IsRaceWord(std::string_view word) {
  if (word.size() < kMinLetters || word.size() > kMaxLetters) return false;
  for (char c : word) {
    if (c < 'a' || c > 'z') return false;
  }
  return true;
}

// SplitMix64: a seed's sequence of draws, the same on every platform.
uint64_t Next(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// Picks tried before a puzzle is refused.
constexpr int kAttempts = 500;

}  // namespace

bool OneLetterApart(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  int differences = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i] && ++differences > 1) return false;
  }
  return differences == 1;
}

Dictionary Dictionary::FromLines(std::string_view text) {
  Dictionary d;
  for (std::string_view line : absl::StrSplit(text, '\n')) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!IsRaceWord(line)) continue;
    if (d.words_.emplace(line).second) {
      d.by_length_[static_cast<int>(line.size())].emplace_back(line);
    }
  }
  for (auto& [length, words] : d.by_length_) std::sort(words.begin(), words.end());
  return d;
}

bool Dictionary::contains(std::string_view word) const {
  return words_.contains(std::string(word));
}

const std::vector<std::string>& Dictionary::wordsOfLength(int letters) const {
  static const std::vector<std::string> kNone;
  const auto found = by_length_.find(letters);
  return found == by_length_.end() ? kNone : found->second;
}

std::optional<std::vector<std::string>> Dictionary::shortestPath(std::string_view start,
                                                                 std::string_view end,
                                                                 int max_words) const {
  if (!contains(start) || !contains(end) || start.size() != end.size()) return std::nullopt;
  std::unordered_map<std::string, std::string> parent{{std::string(start), ""}};
  // Each entry is a word and how many words its ladder from start holds.
  std::deque<std::pair<std::string, int>> frontier{{std::string(start), 1}};
  while (!frontier.empty()) {
    auto [word, words] = std::move(frontier.front());
    frontier.pop_front();
    if (word == end) {
      std::vector<std::string> path;
      for (std::string at = word; !at.empty(); at = parent.at(at)) path.push_back(at);
      std::reverse(path.begin(), path.end());
      return path;
    }
    if (max_words > 0 && words >= max_words) continue;
    for (std::size_t i = 0; i < word.size(); ++i) {
      std::string next = word;
      for (char c = 'a'; c <= 'z'; ++c) {
        if (c == word[i]) continue;
        next[i] = c;
        if (words_.contains(next) && parent.emplace(next, word).second) {
          frontier.emplace_back(next, words + 1);
        }
      }
    }
  }
  return std::nullopt;
}

absl::StatusOr<Puzzle> PickPuzzle(const Dictionary& dictionary, const Dictionary& endpoints,
                                  int letters, uint64_t seed) {
  if (letters < kMinLetters || letters > kMaxLetters) {
    return absl::InvalidArgumentError(
        absl::StrCat("a race is ", kMinLetters, " to ", kMaxLetters, " letters"));
  }
  std::vector<std::string> words;
  for (const std::string& word : endpoints.wordsOfLength(letters)) {
    if (dictionary.contains(word)) words.push_back(word);
  }
  if (words.size() >= 2) {
    uint64_t state = seed;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
      const std::string& start = words[Next(state) % words.size()];
      const std::string& target = words[Next(state) % words.size()];
      const auto path = dictionary.shortestPath(start, target, kMaxOptimal);
      if (!path.has_value()) continue;
      const int optimal = static_cast<int>(path->size());
      if (optimal >= kMinOptimal && optimal <= kMaxOptimal) return Puzzle{start, target, optimal};
    }
  }
  return absl::NotFoundError(absl::StrCat("no ", letters, "-letter puzzle found"));
}

}  // namespace wordchain_race
