#include "domains/games/apis/one_d4_worker/title_roster.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;

/// Answers with whatever rosters it was given, and counts the asking.
class FakeRosters : public TitleSource {
 public:
  absl::StatusOr<std::vector<std::string>> FetchTitled(std::string_view title) override {
    asked.push_back(std::string(title));
    if (on_fetch) on_fetch();
    if (!status.ok()) return status;
    const auto found = rosters.find(std::string(title));
    return found == rosters.end() ? std::vector<std::string>{} : found->second;
  }

  std::map<std::string, std::vector<std::string>> rosters;
  absl::Status status;
  std::vector<std::string> asked;
  std::function<void()> on_fetch;
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
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));

  ASSERT_TRUE(roster.TitleOf("nobody").ok());

  EXPECT_THAT(rosters.asked,
              ElementsAre("GM", "IM", "FM", "WGM", "CM", "NM", "WIM", "WFM", "WCM", "WNM"));
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
  //
  // IM against WGM rather than GM against FM: chess.com lists the two
  // paired, WGM ahead of IM, and reading them in that order would answer
  // WGM where the profile the Java worker reads says IM.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["WGM"] = {"someone"};
  rosters.rosters["IM"] = {"someone"};
  TitleRoster roster(rosters, Options(now));

  EXPECT_EQ(*roster.TitleOf("someone"), "IM");
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
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));

  ASSERT_TRUE(roster.TitleOf("nobody").ok());
  now += absl::ToInt64Seconds(absl::Hours(24));
  ASSERT_TRUE(roster.TitleOf("nobody").ok());

  EXPECT_EQ(rosters.asked.size(), 20u);
}

TEST(TitleRoster, AnswersFromMemoryForTheWholeDayAndNotJustPastTheBackoff) {
  // Two gates stand between a lookup and chess.com, and the backoff is
  // the shorter one. Without the day, the rosters would be re-read every
  // five minutes — the fan-out this exists to remove — and every test
  // that stops at the backoff would still pass.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  now += absl::ToInt64Seconds(absl::Hours(23));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  EXPECT_EQ(rosters.asked.size(), 10u) << "the day expired early";
}

TEST(TitleRoster, ForgetsATitleThatWasTakenAway) {
  // A refresh replaces what it holds. Merged into it instead, the first
  // rosters this process ever read would outlive every correction.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  rosters.rosters["IM"] = {"someone"};
  TitleRoster roster(rosters, Options(now));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  rosters.rosters["GM"].clear();
  now += absl::ToInt64Seconds(absl::Hours(24));

  EXPECT_EQ(*roster.TitleOf("hikaru"), "");
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
  EXPECT_TRUE(roster.Stale()) << "the month it answered for is not complete";
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
  now += absl::ToInt64Seconds(absl::Minutes(5));

  EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");
}

TEST(TitleRoster, IsNotStaleWhileTheRostersAreFresh) {
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  EXPECT_FALSE(roster.Stale());
  now += absl::ToInt64Seconds(absl::Hours(23));
  EXPECT_FALSE(roster.Stale());
  now += absl::ToInt64Seconds(absl::Hours(1));
  EXPECT_TRUE(roster.Stale()) << "stale is the same day the refresh came due";
}

TEST(TitleRoster, DoesNotTakeTenEmptyRostersAsAnAnswer) {
  // Nobody on the whole site holding any title is not a state chess.com
  // can be in. Taking it would untitle everyone while reporting nothing
  // wrong, and every month written under it would be cached that way.
  int64_t now = 0;
  FakeRosters rosters;
  TitleRoster roster(rosters, Options(now));

  EXPECT_FALSE(roster.TitleOf("hikaru").ok());
  EXPECT_EQ(rosters.asked.size(), 10u) << "an empty roster is not itself a failure";
}

TEST(TitleRoster, KeepsTheRostersItHadWhenAllTenComeBackEmpty) {
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, Options(now));
  ASSERT_EQ(*roster.TitleOf("hikaru"), "GM");

  rosters.rosters.clear();
  now += absl::ToInt64Seconds(absl::Hours(25));

  EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");
}

TEST(TitleRoster, StopsRefreshingWhenTheWorkerIsShuttingDown) {
  // Ten sequential calls against a chess.com that is timing out. A
  // shutdown noticed only after the tenth is a process the supervisor
  // kills instead, and a killed process hands its claim back to nobody.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  bool stopping = false;
  rosters.on_fetch = [&stopping] { stopping = true; };

  TitleRoster::Options options = Options(now);
  options.stopping = [&stopping] { return stopping; };
  TitleRoster roster(rosters, std::move(options));

  EXPECT_FALSE(roster.TitleOf("hikaru").ok()) << "a set it abandoned is not a set it has";
  EXPECT_THAT(rosters.asked, ElementsAre("GM")) << "it worked through all ten anyway";
}

TEST(TitleRoster, BacksOffFromWhenTheAttemptEndedRatherThanWhenItStarted) {
  // The failing call is itself slow — that is what failing usually looks
  // like here. Timed from the start, an attempt that outlasts the backoff
  // leaves no backoff at all, and every lookup goes back to chess.com.
  int64_t now = 0;
  FakeRosters rosters;
  rosters.status = absl::UnavailableError("chess.com is timing out");
  rosters.on_fetch = [&now] { now += absl::ToInt64Seconds(absl::Minutes(6)); };
  TitleRoster roster(rosters, Options(now));

  for (int i = 0; i < 5; ++i) EXPECT_FALSE(roster.TitleOf("hikaru").ok());

  EXPECT_THAT(rosters.asked, ElementsAre("GM"));
}

TEST(TitleRoster, AnswersSeveralRunsAtOnce) {
  // One roster serves the whole pool. Every slot looks up two players per
  // game, so this is the hottest shared thing in the worker.
  FakeRosters rosters;
  rosters.rosters["GM"] = {"hikaru"};
  TitleRoster roster(rosters, TitleRoster::Options{});

  std::vector<std::thread> lookers;
  lookers.reserve(4);
  for (int i = 0; i < 4; ++i) {
    lookers.emplace_back([&roster] {
      for (int n = 0; n < 200; ++n) {
        EXPECT_EQ(*roster.TitleOf("hikaru"), "GM");
        EXPECT_EQ(*roster.TitleOf("nobody"), "");
        roster.Stale();
      }
    });
  }
  for (std::thread& looker : lookers) looker.join();

  EXPECT_EQ(rosters.asked.size(), 10u) << "the rosters were read more than once";
}

}  // namespace
}  // namespace one_d4_worker
