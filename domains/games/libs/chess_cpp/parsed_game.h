#ifndef DOMAINS_GAMES_LIBS_CHESS_CPP_PARSED_GAME_H
#define DOMAINS_GAMES_LIBS_CHESS_CPP_PARSED_GAME_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chess_cpp {

/// What a PGN file said, before anyone asks whether it is a legal game.
/// No board here on purpose: this header is the reason `pgn` can be built
/// without a chess library at all.

/// A game's PGN tag pairs, in the order the file listed them.
///
/// Order is kept because it is free to keep and expensive to recover, not
/// because anything reads positionally. Lookup is exact-match and
/// case-sensitive, which is what the PGN spec says tag names are; chess.com
/// writes them consistently ("Event", "ECO", "CurrentPosition").
///
/// A repeated tag keeps its first value. Later duplicates stay visible in
/// entries() — they are malformed input worth being able to see, not worth
/// resolving by guessing.
class Headers {
 public:
  void Add(std::string name, std::string value);

  /// The value for `name`, or nullopt when the tag is absent.
  ///
  /// Borrowed: the result points into this object and dies with it, or with
  /// the next Add() that reallocates.
  std::optional<std::string_view> Get(std::string_view name) const;

  const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }
  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

 private:
  std::vector<std::pair<std::string, std::string>> entries_;
};

/// One game as it came off the wire: its tags, and its moves as SAN tokens.
///
/// The moves are exactly what a replayer should try to play — variations,
/// comments, NAGs, and the result token are gone by this point. They are
/// *not* validated: whether "Qxd8" is legal in this game is a question for
/// Replay(), which owns a board and can answer it.
struct ParsedGame {
  Headers headers;
  std::vector<std::string> san_moves;
};

}  // namespace chess_cpp

#endif  // DOMAINS_GAMES_LIBS_CHESS_CPP_PARSED_GAME_H
