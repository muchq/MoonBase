#include "domains/games/apis/one_d4_worker/title_roster.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;

/// Answers with whatever rosters it was given, and counts the asking.
class FakeRosters : public TitleSource {
 public:
  absl::StatusOr<std::vector<std::string>> FetchTitled(std::string_view title) override {
    asked.push_back(std::string(title));
    if (!status.ok()) return status;
    const auto found = rosters.find(std::string(title));
    return found == rosters.end() ? std::vector<std::string>{} : found->second;
  }

  std::map<std::string, std::vector<std::string>> rosters;
  absl::Status status;
  std::vector<std::string> asked;
};

TitleRoster::Options Options(int64_t& now) {
  TitleRoster::Options options;
  options.good_for = absl::Hours(24);
  options.now = [&now] { return absl::FromUnixSeconds(now); };
  return options;
}

TEST(TitleRoster, ReadsEveryTitleOnce) {
  int64_t now = 0;
  FakeRosters rosters;
  TitleRoster roster(rosters, Options(now));

  ASSERT_TRUE(roster.TitleOf("nobody").ok());

  EXPECT_THAT(rosters.asked,
              ElementsAre("GM", "WGM", "IM", "WIM", "FM", "WFM", "NM", "WNM", "CM", "WCM"));
}

TEST(TitleRoster, FindsAPlayerInTheRoster) {
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  rosters.rosters["WIM"] = {"someone"};
  TitleRoster roster(rosters, Options(now));

  EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");
  EXPECT_EQ(*roster.TitleOf("someone"), "WIM");
}

TEST(TitleRoster, SaysNothingAboutAnUntitledPlayer) {
  // The common case by a wide margin, and the reason this exists: it is
  // an answer, not a lookup that failed.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));

  const absl::StatusOr<std::string> title = roster.TitleOf("some_patzer");

  ASSERT_TRUE(title.ok()) << title.status();
  EXPECT_EQ(*title, "");
}

TEST(TitleRoster, MatchesUsernamesWhateverCaseEitherSideUsed) {
  // Both sides, because either alone leaves the other free to drift. The
  // rosters came back lowercase the day this was written, and an archive
  // names the same player however they typed it — but neither is a
  // promise, and matching on the raw string titles nobody.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru", "MagnusCarlsen"};
  TitleRoster roster(rosters, Options(now));

  EXPECT_EQ(*roster.TitleOf("HiKaRu"), "GM") << "the archive's spelling was not normalised";
  EXPECT_EQ(*roster.TitleOf("magnuscarlsen"), "GM") << "the roster's spelling was not normalised";
}

TEST(TitleRoster, KeepsTheStrongerTitleWhenTwoRostersNameOnePlayer) {
  // One player, one title — but the rosters are ten separate documents
  // fetched at ten different instants, so a title awarded between two of
  // them can appear in both. Order decides, and it decides the same way
  // every time.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"someone"};
  rosters.rosters["FM"] = {"someone"};
  TitleRoster roster(rosters, Options(now));

  EXPECT_EQ(*roster.TitleOf("someone"), "GM");
}

TEST(TitleRoster, ReadsTheRostersOnceAndThenAnswersFromMemory) {
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));

  for (int i = 0; i < 50; ++i) EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");

  EXPECT_EQ(rosters.asked.size(), 10u) << "a lookup went back to chess.com";
}

TEST(TitleRoster, ReadsThemAgainOnceTheyAreStale) {
  // Titles are awarded, so the set moves — on a scale of months, which is
  // why a day is generous rather than eager.
  int64_t now = 0;
  FakeRosters rosters;
  TitleRoster roster(rosters, Options(now));

  ASSERT_TRUE(roster.TitleOf("nobody").ok());
  now += absl::ToInt64Seconds(absl::Hours(25));
  ASSERT_TRUE(roster.TitleOf("nobody").ok());

  EXPECT_EQ(rosters.asked.size(), 20u);
}

TEST(TitleRoster, SaysSoWhenItHasNoRosterAtAll) {
  // Distinct from "untitled": the caller marks the month incomplete so a
  // later request refetches it, rather than freezing a null title.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.status = absl::UnavailableError("chess.com is down");
  TitleRoster roster(rosters, Options(now));

  EXPECT_FALSE(roster.TitleOf("hikaru").ok());
}

TEST(TitleRoster, KeepsAnsweringFromAStaleRosterWhenARefreshFails) {
  // A roster from yesterday is a better answer than none, and a title
  // this worker has not heard about yet is a smaller wrong than every
  // player in the month losing the one it had.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  rosters.status = absl::UnavailableError("chess.com is down");
  now += absl::ToInt64Seconds(absl::Hours(25));

  const absl::StatusOr<std::string> title = roster.TitleOf("hikaru");
  ASSERT_TRUE(title.ok()) << title.status();
  EXPECT_EQ(*title, "GM");
}

TEST(TitleRoster, DoesNotRetryEveryLookupWhileChessComIsDown) {
  // A retry per lookup would turn one bad minute into the fan-out this
  // exists to remove, with nothing to show for it. And a refresh that
  // fails stops at the first title rather than asking nine more times
  // for a set it is going to throw away.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.status = absl::UnavailableError("chess.com is down");
  TitleRoster roster(rosters, Options(now));

  for (int i = 0; i < 5; ++i) EXPECT_FALSE(roster.TitleOf("hikaru").ok());

  EXPECT_THAT(rosters.asked, ElementsAre("GM"));
}

TEST(TitleRoster, TriesAgainOnceTheBackoffHasPassed) {
  int64_t now = 0;
  FakeRosters rosters;
  rosters.status = absl::UnavailableError("chess.com is down");
  TitleRoster roster(rosters, Options(now));
  ASSERT_FALSE(roster.TitleOf("hikaru").ok());

  rosters.status = absl::OkStatus();
  rosters.rosters["GM"] = {"hikaru"};
  now += absl::ToInt64Seconds(absl::Minutes(6));

  EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");
}

}  // namespace
}  // namespace one_d4_worker
