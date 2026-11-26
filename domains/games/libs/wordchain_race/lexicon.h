#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H

#include <string_view>

#include "domains/games/libs/wordchain_race/dictionary.h"

namespace wordchain_race {

/// The lexicon a race is played against: its name, the words a runner may
/// play, the common words puzzles start and end on, and the notice their
/// licenses ask every copy to carry. Holding the notice here keeps it in
/// any binary that plays a race.
struct Lexicon {
  std::string_view name;
  const Dictionary& words;
  const Dictionary& endpoints;
  std::string_view notice;
};

/// Filtered ENABLE words (:race_words) with SCOWL's scrubbed common words
/// (common_words.txt) for endpoints, built into the binary and read once.
[[nodiscard]] const Lexicon& EnableLexicon();

}  // namespace wordchain_race

#endif  // DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H
