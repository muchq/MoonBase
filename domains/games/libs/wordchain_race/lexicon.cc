#include "domains/games/libs/wordchain_race/lexicon.h"

#include "domains/games/libs/wordchain_race/lexicon_data.h"

namespace wordchain_race {

const Lexicon& EnableLexicon() {
  static const Dictionary* const kWords = new Dictionary(Dictionary::FromLines(kRaceWordsText));
  static const Dictionary* const kEndpoints =
      new Dictionary(Dictionary::FromLines(kCommonWordsText));
  static const Lexicon* const kLexicon =
      new Lexicon{"enable", *kWords, *kEndpoints, kScowlCopyrightText};
  return *kLexicon;
}

}  // namespace wordchain_race
