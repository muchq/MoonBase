#include "domains/games/apis/one_d4_worker/openings.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace one_d4_worker {
namespace {

using ::testing::Contains;
using ::testing::Not;

TEST(Openings, ReadsTheSlugAsASpacedName) {
  EXPECT_EQ(OpeningNameFromEcoUrl("https://www.chess.com/openings/Vienna-Game-Max-Lange-Defense"),
            "Vienna Game Max Lange Defense");
  EXPECT_EQ(OpeningNameFromEcoUrl("https://www.chess.com/openings/Vienna-Game/"), "Vienna Game");
}

TEST(Openings, KeepsTheMoveContinuationInTheName) {
  // The continuation is dropped for the family only. Hoisting that up here
  // would silently coarsen opening_name too.
  EXPECT_EQ(OpeningNameFromEcoUrl(
                "https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack-3...dxe4"),
            "Caro Kann Defense Two Knights Attack 3...dxe4");
}

TEST(Openings, HasNoNameForACodeOrABlank) {
  EXPECT_EQ(OpeningNameFromEcoUrl("B10"), "");
  EXPECT_EQ(OpeningNameFromEcoUrl("https://www.chess.com/openings/B10"), "");
  EXPECT_EQ(OpeningNameFromEcoUrl(""), "");
  EXPECT_EQ(OpeningNameFromEcoUrl("  "), "");
  EXPECT_EQ(OpeningNameFromEcoUrl("https://www.chess.com/openings/"), "");
}

TEST(Openings, FamilyStopsAtTheFirstStructuralWord) {
  EXPECT_EQ(OpeningFamilyFromName("English Opening Agincourt Defense 2.Nf3 d5 3.g3"),
            "English Opening");
  EXPECT_EQ(OpeningFamilyFromName("Caro Kann Defense Two Knights Attack 3...dxe4"),
            "Caro Kann Defense");
  EXPECT_EQ(OpeningFamilyFromName("Kings Indian Attack"), "Kings Indian Attack");
  EXPECT_EQ(OpeningFamilyFromName("Queens Gambit Declined Modern Variation"), "Queens Gambit");
  EXPECT_EQ(OpeningFamilyFromName("London System"), "London System");
}

TEST(Openings, FamilyDropsAContinuationGluedToTheStructuralWord) {
  // "Owens Defense...3.Nc3" splits as one word, so a scan that does not cut
  // the continuation first never sees "Defense" and truncates to two words.
  EXPECT_EQ(OpeningFamilyFromName("Owens Defense...3.Nc3 e6 4.Nf3 Bb4"), "Owens Defense");
  EXPECT_EQ(OpeningFamilyFromName("Giuoco Piano Game...5.d3 d6 6.c3 O O 7.Re1"),
            "Giuoco Piano Game");
  EXPECT_EQ(OpeningFamilyFromName("Kings Indian Attack...3.Bg2 e5"), "Kings Indian Attack");
  EXPECT_EQ(OpeningFamilyFromName("Sicilian Defense Chekhover Variation...7.Nc3 Nf6"),
            "Sicilian Defense");
  EXPECT_EQ(OpeningFamilyFromName("Owens Defense...3.Nc3 e6 4...Bb4"), "Owens Defense");
}

TEST(Openings, FamilyFallsBackToTheFirstTwoWords) {
  EXPECT_EQ(OpeningFamilyFromName("Something Unusual Line Here"), "Something Unusual");
  EXPECT_EQ(OpeningFamilyFromName("Singleword"), "Singleword");
  EXPECT_EQ(OpeningFamilyFromName("Grob...2.Bg2 e5"), "Grob");
  EXPECT_EQ(OpeningFamilyFromName("Something Unusual...2.Nf3 d5 3.g3"), "Something Unusual");
}

TEST(Openings, HasNoFamilyForABlankOrAContinuationAlone) {
  EXPECT_EQ(OpeningFamilyFromName(""), "");
  EXPECT_EQ(OpeningFamilyFromName(" "), "");
  EXPECT_EQ(OpeningFamilyFromName("...3.Nc3 e6"), "");
  EXPECT_EQ(OpeningFamilyFromName("  ...  "), "");
}

// --- the same corpus the Java derivation is held to ------------------------

// OpeningsCorpusTest's numbers, over the same frozen file. They are the
// positive control: a derivation that stops collapsing continuations moves
// the family count (52 before #1344, 40 after) even when every individual
// key still looks clean.
constexpr int kEcoUrls = 500;
constexpr std::size_t kDistinctSlugs = 307;
constexpr std::size_t kDistinctFamilies = 40;

std::vector<std::string> CorpusEcoUrls() {
  std::ifstream file("domains/games/apis/one_d4/src/test/resources/hikaru_corpus.pgn");
  EXPECT_TRUE(file.good()) << "hikaru_corpus.pgn is not where this test looks";
  std::ostringstream contents;
  contents << file.rdbuf();
  const std::string text = contents.str();

  std::vector<std::string> urls;
  const std::regex tag(R"re(\[ECOUrl "([^"]+)"\])re");
  for (std::sregex_iterator it(text.begin(), text.end(), tag), end; it != end; ++it) {
    urls.push_back((*it)[1]);
  }
  return urls;
}

std::set<std::string> CorpusFamilies() {
  const std::vector<std::string> urls = CorpusEcoUrls();
  EXPECT_EQ(urls.size(), static_cast<std::size_t>(kEcoUrls));
  EXPECT_EQ(std::set<std::string>(urls.begin(), urls.end()).size(), kDistinctSlugs);

  std::set<std::string> families;
  for (const std::string& url : urls) {
    const std::string name = OpeningNameFromEcoUrl(url);
    EXPECT_FALSE(name.empty()) << "no name for " << url;
    const std::string family = OpeningFamilyFromName(name);
    EXPECT_FALSE(family.empty()) << "no family for " << url;
    families.insert(family);
  }
  return families;
}

TEST(Openings, NoCorpusFamilyCarriesAMoveContinuation) {
  // Digits and '.' are the tell: no opening name carries either.
  for (const std::string& family : CorpusFamilies()) {
    EXPECT_EQ(family.find('.'), std::string::npos) << family;
    EXPECT_EQ(family.find_first_of("0123456789"), std::string::npos) << family;
  }
}

TEST(Openings, DerivesTheSameFamiliesAsTheJavaWorker) {
  const std::set<std::string> families = CorpusFamilies();
  EXPECT_EQ(families.size(), kDistinctFamilies);
  EXPECT_THAT(families, Contains("Giuoco Piano Game"));
  EXPECT_THAT(families, Contains("Kings Indian Attack"));
  EXPECT_THAT(families, Contains("Old Benoni Defense"));
  EXPECT_THAT(families, Contains("Owens Defense"));
  EXPECT_THAT(families, Not(Contains("Giuoco Piano")));
  EXPECT_THAT(families, Not(Contains("Kings Indian")));
  EXPECT_THAT(families, Not(Contains("Old Benoni")));
}

}  // namespace
}  // namespace one_d4_worker
