// The C++ port against the Java pipeline, over 500 real games.
//
// The oracle is motif_parity_golden.tsv — what the Java pipeline extracts
// from hikaru_corpus.pgn, checked into that package and kept current by its
// own MotifGoldenTest. Neither half is any use alone: a golden nobody
// regenerates stops describing anything, and a comparison against a file
// this side generated agrees with itself.
//
// The port reproduces every row the Java pipeline writes, and writes seven
// motifs' worth that Java never has. Both halves are pinned row for row —
// "allowed to differ" without identity is how a regression hides inside a
// known difference.
//
// The two used to disagree about Black's ply as well. That was a Java bug,
// fixed rather than tolerated (MotifOccurrence.plyOf), so the harness no
// longer transforms the oracle before comparing it.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/side.h"
#include "domains/games/libs/one_d4_motifs/extract.h"
#include "domains/games/libs/one_d4_motifs/motif.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4 {
namespace {

using ::testing::IsEmpty;

constexpr char kCorpus[] = "domains/games/apis/one_d4/src/test/resources/hikaru_corpus.pgn";
constexpr char kGolden[] = "domains/games/apis/one_d4/src/test/resources/motif_parity_golden.tsv";
constexpr char kCppOnlyRows[] = "domains/games/libs/one_d4_motifs/testdata/cpp_only_rows.tsv";
constexpr int kGames = 500;

std::string Read(const std::string& path) {
  std::ifstream stream(path);
  EXPECT_TRUE(stream.good()) << "missing " << path;
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

std::vector<std::string> SplitGames(const std::string& pgn) {
  std::vector<std::string> games;
  std::string current;
  for (const std::string_view line : absl::StrSplit(pgn, '\n')) {
    if (absl::StartsWith(line, "[Event \"") && !current.empty() &&
        current.find("\n1.") != std::string::npos) {
      games.push_back(current);
      current.clear();
    }
    absl::StrAppend(&current, line, "\n");
  }
  if (!current.empty()) games.push_back(current);
  return games;
}

std::string Or(const std::optional<std::string>& value) { return value.value_or("-"); }

/// A golden row, from either side. Carries `description` because it is a
/// stored column the API hands to clients, and nothing else compares it.
std::string Row(int game, const MotifOccurrence& occurrence) {
  return absl::StrFormat(
      "%04d\t%s\t%04d\t%s\t%d\t%s\t%s\t%s\t%s\t%d\t%d\t%s", game, ToString(occurrence.motif),
      occurrence.ply, chess_cpp::ToString(occurrence.side), occurrence.move_number,
      occurrence.description, Or(occurrence.attacker), Or(occurrence.target),
      Or(occurrence.moved_piece), occurrence.is_discovered ? 1 : 0, occurrence.is_mate ? 1 : 0,
      occurrence.pin_type.has_value() ? std::string(ToString(*occurrence.pin_type)) : "-");
}

std::multiset<std::string> CppRows() {
  const std::vector<std::string> games = SplitGames(Read(kCorpus));
  EXPECT_EQ(games.size(), kGames);

  std::multiset<std::string> rows;
  for (int i = 0; i < static_cast<int>(games.size()); ++i) {
    const auto parsed = chess_cpp::ParseGame(games[i]);
    EXPECT_TRUE(parsed.ok()) << "game " << i << ": " << parsed.status();
    if (!parsed.ok()) continue;
    const auto features = Extract(*parsed);
    EXPECT_TRUE(features.ok()) << "game " << i << ": " << features.status();
    if (!features.ok()) continue;
    for (const MotifOccurrence& occurrence : features->occurrences) {
      rows.insert(Row(i, occurrence));
    }
  }
  return rows;
}

std::multiset<std::string> GoldenRows() {
  std::multiset<std::string> rows;
  for (const std::string_view line : absl::StrSplit(Read(kGolden), '\n')) {
    if (line.empty()) continue;
    rows.emplace(line);
  }
  return rows;
}

std::vector<std::string> Missing(const std::multiset<std::string>& from,
                                 const std::multiset<std::string>& in) {
  std::vector<std::string> missing;
  std::multiset<std::string> remaining = in;
  for (const std::string& row : from) {
    const auto found = remaining.find(row);
    if (found == remaining.end()) {
      missing.push_back(row);
    } else {
      remaining.erase(found);
    }
  }
  return missing;
}

std::map<std::string, int> ByMotif(const std::vector<std::string>& rows) {
  std::map<std::string, int> counts;
  for (const std::string& row : rows) {
    const std::vector<std::string> fields = absl::StrSplit(row, '\t');
    if (fields.size() > 1) ++counts[fields[1]];
  }
  return counts;
}

class Parity : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    cpp_ = new std::multiset<std::string>(CppRows());
    golden_ = new std::multiset<std::string>(GoldenRows());
  }
  static std::multiset<std::string>* cpp_;
  static std::multiset<std::string>* golden_;
};

std::multiset<std::string>* Parity::cpp_ = nullptr;
std::multiset<std::string>* Parity::golden_ = nullptr;

/// The Java pipeline's own count over this bank, pinned so a regenerated
/// corpus or golden is loud rather than quiet.
constexpr int kGoldenRows = 14548;

TEST_F(Parity, ReadsTheWholeGolden) { EXPECT_EQ(golden_->size(), kGoldenRows); }

TEST_F(Parity, FindsEverythingTheJavaPipelineFinds) {
  // Nothing lost: every row the Java pipeline writes, compared row for row
  // with no transform in between. Losing one is a port bug.
  const std::vector<std::string> lost = Missing(*golden_, *cpp_);
  EXPECT_THAT(lost, IsEmpty()) << "rows the port stopped producing: " << lost.size() << " of "
                               << golden_->size();
}

TEST_F(Parity, EmitsExactlyTheRowsJavaCannot) {
  // Seven motifs Java has never stored a row for, pinned row for row rather
  // than counted. Four of them the read path derives from ATTACK rows when
  // a query asks — so ORDER BY motif_count, which counts stored rows, has
  // always counted zero for them — and three no implementation has ever
  // produced at all.
  std::vector<std::string> found = Missing(*cpp_, *golden_);
  std::sort(found.begin(), found.end());

  std::vector<std::string> expected;
  for (const std::string_view line : absl::StrSplit(Read(kCppOnlyRows), '\n')) {
    if (!line.empty()) expected.emplace_back(line);
  }

  EXPECT_EQ(found.size(), expected.size());
  EXPECT_EQ(found, expected);
}

TEST_F(Parity, TheExtraRowsAreThoseSevenMotifsAndNothingElse) {
  // The readable summary of the file above. A detector that started firing
  // somewhere new moves one of these before the row-for-row diff has to be
  // read.
  const std::map<std::string, int> extra = ByMotif(Missing(*cpp_, *golden_));
  EXPECT_EQ(extra, (std::map<std::string, int>{
                       {"CHECKMATE", 41},
                       {"CROSS_PIN", 43},
                       {"DISCOVERED_ATTACK", 2892},
                       {"DISCOVERED_CHECK", 31},
                       {"DOUBLE_CHECK", 3},
                       {"OVERLOADED_PIECE", 200},
                       {"ZUGZWANG", 10},
                   }));
}

}  // namespace
}  // namespace one_d4
