#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H

#include <string_view>

#include "domains/games/libs/wordchain_race/dictionary.h"

namespace wordchain_race {

/// The lexicon a race is played against: its name, the words a runner may
/// play, and the common words puzzles start and end on.
struct Lexicon {
  std::string_view name;
  const Dictionary& words;
  const Dictionary& endpoints;
};

/// ENABLE's words (:race_words) with SCOWL's common words (:common_words)
/// for endpoints, built into the binary and read once.
[[nodiscard]] const Lexicon& EnableLexicon();

/// SCOWL's copyright notice, which its license asks every copy of a list
/// derived from it to carry.
[[nodiscard]] std::string_view ScowlCopyright();

}  // namespace wordchain_race

#endif  // DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_LEXICON_H
