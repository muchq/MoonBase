// The C++ port against the Java pipeline, over 500 real games.
//
// The oracle is motif_parity_golden.tsv — what the Java pipeline extracts
// from hikaru_corpus.pgn, checked into that package and kept current by its
// own MotifGoldenTest. Neither half is any use alone: a golden nobody
// regenerates stops describing anything, and a comparison against a file
// this side generated agrees with itself.
//
// The two implementations agree on every row but one motif's, on purpose:
// CROSS_PIN could never fire in Java. That difference is pinned row for row
// rather than counted — "allowed to differ" without identity is how a
// regression hides inside a known difference.
//
// They also used to disagree about Black's ply. That was a Java bug, and it
// is fixed rather than tolerated (MotifOccurrence.plyOf), so the harness no
// longer transforms the oracle before comparing it.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
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
constexpr char kCrossPinRows[] = "domains/games/libs/one_d4_motifs/testdata/cross_pins.tsv";
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

/// A golden row, from either side.
std::string Row(int game, const MotifOccurrence& occurrence) {
  return absl::StrFormat(
      "%04d\t%s\t%04d\t%s\t%d\t%s\t%s\t%s\t%d\t%d\t%s", game, ToString(occurrence.motif),
      occurrence.ply, chess_cpp::ToString(occurrence.side), occurrence.move_number,
      Or(occurrence.attacker), Or(occurrence.target), Or(occurrence.moved_piece),
      occurrence.is_discovered ? 1 : 0, occurrence.is_mate ? 1 : 0,
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
constexpr int kGoldenRows = 14558;

/// Cross-pins the Java detector cannot see. It looks for one square found
/// twice from the same king, which two rays never do, so CROSS_PIN has
/// never had a single row in it.
constexpr int kCrossPins = 43;

TEST_F(Parity, ReadsTheWholeGolden) { EXPECT_EQ(golden_->size(), kGoldenRows); }

TEST_F(Parity, FindsEverythingTheJavaPipelineFinds) {
  // Nothing lost: 14,558 rows over 500 games and ten detectors, compared
  // row for row with no transform in between. Losing one is a port bug.
  const std::vector<std::string> lost = Missing(*golden_, *cpp_);
  EXPECT_THAT(lost, IsEmpty()) << "rows the port stopped producing: " << lost.size() << " of "
                               << golden_->size();
}

TEST_F(Parity, AddsTheCrossPinsJavaCannotSee) {
  // Row for row, not just 43 of them: Java emits none, so these are the one
  // motif the golden cannot hold to account. A rewrite of the detector that
  // found 43 different cross-pins would otherwise pass.
  std::vector<std::string> found;
  for (const std::string& row : Missing(*cpp_, *golden_)) {
    if (absl::StrContains(row, "\tCROSS_PIN\t")) found.push_back(row);
  }
  std::sort(found.begin(), found.end());

  std::vector<std::string> expected;
  for (const std::string_view line : absl::StrSplit(Read(kCrossPinRows), '\n')) {
    if (!line.empty()) expected.emplace_back(line);
  }

  EXPECT_EQ(expected.size(), kCrossPins);
  EXPECT_EQ(found, expected);
}

TEST_F(Parity, DiffersOnlyInTheCrossPins) {
  const std::vector<std::string> extra = Missing(*cpp_, *golden_);
  EXPECT_EQ(extra.size(), kCrossPins);
  for (const auto& [motif, count] : ByMotif(extra)) {
    std::cerr << "extra " << motif << ": " << count << "\n";
  }
}

}  // namespace
}  // namespace one_d4
