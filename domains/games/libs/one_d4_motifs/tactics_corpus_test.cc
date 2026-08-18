// The second bank: 150 games chosen for the moves ordinary play barely
// contains — underpromotion, promotion with check, mate.
//
// No Java oracle here, and that is the point of the bank rather than a gap
// in it: four of these games are odds games with a [SetUp] tag, which the
// Java pipeline replays from the standard position, fails on, and indexes
// as having no motifs at all. What this pins instead is that the port sees
// the motifs the wide-net bank has too few of — hikaru_corpus holds one
// promotion-with-checkmate and no smothered mate at all.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/side.h"
#include "domains/games/libs/one_d4_motifs/extract.h"
#include "domains/games/libs/one_d4_motifs/motif.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4 {
namespace {

constexpr char kCorpus[] = "domains/games/libs/chess_cpp/testdata/tactics_corpus.pgn";
constexpr int kGames = 150;

struct Extracted {
  int games = 0;
  int odds_games = 0;
  int occurrences = 0;
  std::map<std::string, int> by_motif;
  std::vector<std::string> malformed;
};

/// Says what is wrong with an occurrence, or nothing.
std::string Malformed(const MotifOccurrence& occurrence) {
  if (occurrence.ply < 1) return "ply below 1";
  if (occurrence.move_number < 1) return "move number below 1";
  if (occurrence.ply !=
      2 * occurrence.move_number - (occurrence.side == chess_cpp::Side::kWhite ? 1 : 0)) {
    return "ply disagrees with move number and side";
  }
  if (occurrence.description.empty()) return "no description";
  if (occurrence.pin_type.has_value() && occurrence.motif != Motif::kPin) {
    return "pin type on something that is not a pin";
  }
  if (occurrence.is_discovered && !occurrence.moved_piece.has_value()) {
    return "discovered with no moved piece";
  }
  for (const std::optional<std::string>* field : {&occurrence.attacker, &occurrence.target}) {
    // Letter plus square, the spelling ChessQL matches on.
    if (field->has_value() && (*field)->size() != 3) {
      return "piece notation is not letter + square";
    }
  }
  return "";
}

Extracted RunCorpus() {
  std::ifstream file(kCorpus);
  EXPECT_TRUE(file.good()) << "missing " << kCorpus;
  std::ostringstream contents;
  contents << file.rdbuf();

  std::vector<std::string> games;
  std::string current;
  for (const std::string_view line : absl::StrSplit(contents.str(), '\n')) {
    if (absl::StartsWith(line, "[Event \"") && !current.empty() &&
        current.find("\n1.") != std::string::npos) {
      games.push_back(current);
      current.clear();
    }
    absl::StrAppend(&current, line, "\n");
  }
  if (!current.empty()) games.push_back(current);

  Extracted result;
  for (std::size_t i = 0; i < games.size(); ++i) {
    const auto parsed = chess_cpp::ParseGame(games[i]);
    EXPECT_TRUE(parsed.ok()) << "game " << i << ": " << parsed.status();
    if (!parsed.ok()) continue;
    if (parsed->headers.Get("SetUp") == "1") ++result.odds_games;

    const auto features = Extract(*parsed);
    EXPECT_TRUE(features.ok()) << "game " << i << ": " << features.status();
    if (!features.ok()) continue;

    ++result.games;
    result.occurrences += static_cast<int>(features->occurrences.size());
    for (const MotifOccurrence& occurrence : features->occurrences) {
      ++result.by_motif[std::string(ToString(occurrence.motif))];
      const std::string wrong = Malformed(occurrence);
      if (!wrong.empty()) {
        result.malformed.push_back(absl::StrCat("game ", i, " ply ", occurrence.ply, ": ", wrong));
      }
    }
  }
  return result;
}

int Count(const Extracted& extracted, const std::string& motif) {
  const auto found = extracted.by_motif.find(motif);
  return found == extracted.by_motif.end() ? 0 : found->second;
}

/// Extracted once for the suite: the corpus is 150 games through ten
/// detectors, and running it per test is the difference between a fast
/// test and a slow one under the sanitizers.
class TacticsCorpus : public testing::Test {
 protected:
  static void SetUpTestSuite() { extracted_ = new Extracted(RunCorpus()); }
  static const Extracted& corpus() { return *extracted_; }

 private:
  static Extracted* extracted_;
};

Extracted* TacticsCorpus::extracted_ = nullptr;

TEST_F(TacticsCorpus, EveryGameExtracts) {
  const Extracted& extracted = corpus();
  EXPECT_EQ(extracted.games, kGames);
  EXPECT_EQ(extracted.odds_games, 4) << "the games the Java pipeline cannot replay at all";
}

TEST_F(TacticsCorpus, CoversTheMotifsTheWideBankIsThinOn) {
  // Exact, because the bank is frozen: a detector that quietly stops firing
  // moves these, and ">0" would not notice.
  const Extracted& extracted = corpus();
  EXPECT_EQ(Count(extracted, "PROMOTION"), 148);
  EXPECT_EQ(Count(extracted, "PROMOTION_WITH_CHECK"), 69);
  EXPECT_EQ(Count(extracted, "PROMOTION_WITH_CHECKMATE"), 4);
  EXPECT_EQ(Count(extracted, "BACK_RANK_MATE"), 15);
  EXPECT_EQ(Count(extracted, "CROSS_PIN"), 14);
  EXPECT_EQ(Count(extracted, "ATTACK"), 3798);
  EXPECT_EQ(Count(extracted, "CHECK"), 1467);
  EXPECT_EQ(Count(extracted, "PIN"), 874);
  EXPECT_EQ(Count(extracted, "SKEWER"), 123);
}

TEST_F(TacticsCorpus, HoldsNoSmotheredMate) {
  // Neither bank does. SMOTHERED_MATE rests on detectors_test alone, which
  // is worth knowing rather than assuming — a corpus that grows one should
  // fail here and take the count with it.
  EXPECT_EQ(Count(corpus(), "SMOTHERED_MATE"), 0);
}

TEST_F(TacticsCorpus, EveryOccurrenceIsWellFormed) {
  const Extracted& extracted = corpus();
  EXPECT_EQ(extracted.occurrences, 6512);
  // Every row goes to a NOT NULL column or a typed field; these are the
  // invariants the write path would otherwise discover in production.
  EXPECT_TRUE(extracted.malformed.empty()) << absl::StrJoin(extracted.malformed, "\n");
}

}  // namespace
}  // namespace one_d4
