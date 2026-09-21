#include "domains/platform/libs/event_log/event_log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace event_log {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

absl::Time At(int year, int month, int day, int hour, int minute) {
  return absl::FromCivil(absl::CivilMinute(year, month, day, hour, minute), absl::UTCTimeZone());
}

class EventLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("event_log_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  std::string Dir() const { return dir_.string(); }

  std::vector<std::string> FileNames() const {
    std::vector<std::string> names;
    if (!std::filesystem::exists(dir_)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
      names.push_back(entry.path().filename().string());
    }
    return names;
  }

  std::vector<std::string> LinesOf(const std::string& file_name) const {
    std::ifstream in(dir_ / file_name);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
  }

  /// Backdates the active file, which is the only record a restart has
  /// of what hour the lines in it belong to.
  void SetModified(const std::string& file_name, absl::Time when) {
    std::filesystem::last_write_time(dir_ / file_name,
                                     std::chrono::file_clock::from_sys(absl::ToChronoTime(when)));
  }

  std::filesystem::path dir_;
};

TEST_F(EventLogTest, AppendsEachEventAsItsOwnLineOfTheActiveFile) {
  auto log = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(log.ok()) << log.status();
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 0), R"({"n":1})").ok());
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 59), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(), ElementsAre("game_events.log"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"n":1})", R"({"n":2})"));
}

// The shipper only takes a file whose name carries a timestamp, so the
// active one must never have one — that is what keeps it from uploading
// and deleting a file still being written.
TEST_F(EventLogTest, TheActiveFileIsNamedWithoutATimestamp) {
  EXPECT_EQ(EventLog::ActiveName("game_events"), "game_events.log");
  EXPECT_EQ(EventLog::RolledName("game_events", At(2026, 9, 21, 13, 40)),
            "game_events-2026-09-21T13.log");
}

TEST_F(EventLogTest, RollsTheActiveFileAtTheFirstWriteOfANewHour) {
  auto log = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(log.ok()) << log.status();
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 5), R"({"n":1})").ok());
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 14, 5), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(),
              UnorderedElementsAre("game_events.log", "game_events-2026-09-21T13.log"));
  EXPECT_THAT(LinesOf("game_events-2026-09-21T13.log"), ElementsAre(R"({"n":1})"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"n":2})"));
}

// The stamp names the hour the lines are from, not the hour the roll
// happened in. An idle service rolls late — that is the trade for having
// no timer thread — and a name taken from the roll's own clock would
// file a quiet evening's events under the morning that ended it.
TEST_F(EventLogTest, TheRolledNameCarriesTheHourItsLinesCover) {
  auto log = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(log.ok()) << log.status();
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 59), R"({"n":1})").ok());
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 19, 30), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(),
              UnorderedElementsAre("game_events.log", "game_events-2026-09-21T13.log"));
  EXPECT_THAT(LinesOf("game_events-2026-09-21T13.log"), ElementsAre(R"({"n":1})"));
}

// A reopen is a restart. The lines already on disk are from the previous
// run's hour — its last write is the only record of which — and rolling
// them under the hour of the first write after the restart would misfile
// them.
TEST_F(EventLogTest, AReopenRollsWhatTheLastRunLeftUnderItsOwnHour) {
  {
    auto log = EventLog::Open(Dir(), "game_events");
    ASSERT_TRUE(log.ok()) << log.status();
    ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 5), R"({"n":1})").ok());
  }
  SetModified("game_events.log", At(2026, 9, 21, 13, 5));

  auto reopened = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(reopened.ok()) << reopened.status();
  ASSERT_TRUE((*reopened)->Append(At(2026, 9, 21, 14, 5), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(),
              UnorderedElementsAre("game_events.log", "game_events-2026-09-21T13.log"));
  EXPECT_THAT(LinesOf("game_events-2026-09-21T13.log"), ElementsAre(R"({"n":1})"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"n":2})"));
}

// A restart inside the hour picks the leftover file back up rather than
// rolling it: an hour is one file, and a deploy is not an event.
TEST_F(EventLogTest, AReopenInsideTheSameHourKeepsWritingTheSameFile) {
  {
    auto log = EventLog::Open(Dir(), "game_events");
    ASSERT_TRUE(log.ok()) << log.status();
    ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 5), R"({"n":1})").ok());
  }
  SetModified("game_events.log", At(2026, 9, 21, 13, 5));

  auto reopened = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(reopened.ok()) << reopened.status();
  ASSERT_TRUE((*reopened)->Append(At(2026, 9, 21, 13, 40), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(), ElementsAre("game_events.log"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"n":1})", R"({"n":2})"));
}

// An hour rolls once per run, so a second file wanting a name the first
// already took needs a restart and a clock that went backwards over it.
// Rare, and a silent overwrite would be a deleted hour of games, so the
// second lands beside the first under a name the shipper matches too.
TEST_F(EventLogTest, ARollOntoANameAlreadyTakenKeepsBothFiles) {
  // Each run opens, appends one line in the hour after the leftover's,
  // and so rolls that leftover away.
  const auto run = [&](const std::string& line) {
    auto log = EventLog::Open(Dir(), "game_events");
    ASSERT_TRUE(log.ok()) << log.status();
    ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 14, 0), line).ok());
  };
  {
    auto first = EventLog::Open(Dir(), "game_events");
    ASSERT_TRUE(first.ok()) << first.status();
    ASSERT_TRUE((*first)->Append(At(2026, 9, 21, 13, 5), R"({"run":1})").ok());
  }
  SetModified("game_events.log", At(2026, 9, 21, 13, 5));
  run(R"({"run":2})");

  // The clock went back over the restart, so this run believes its
  // leftover belongs to the hour that has already rolled.
  SetModified("game_events.log", At(2026, 9, 21, 13, 50));
  run(R"({"run":3})");

  EXPECT_THAT(FileNames(), UnorderedElementsAre("game_events.log", "game_events-2026-09-21T13.log",
                                                "game_events-2026-09-21T13-1.log"));
  EXPECT_THAT(LinesOf("game_events-2026-09-21T13.log"), ElementsAre(R"({"run":1})"));
  EXPECT_THAT(LinesOf("game_events-2026-09-21T13-1.log"), ElementsAre(R"({"run":2})"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"run":3})"));
}

// Two events can be handed over out of order — the timestamp is the
// caller's, not the writer's. Rolling backwards would name a file for an
// hour that already rolled, so a late line is simply filed where it is.
TEST_F(EventLogTest, AnOlderTimestampAppendsWhereItIsRatherThanRollingBackwards) {
  auto log = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(log.ok()) << log.status();
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 14, 5), R"({"n":1})").ok());
  ASSERT_TRUE((*log)->Append(At(2026, 9, 21, 13, 5), R"({"n":2})").ok());

  EXPECT_THAT(FileNames(), ElementsAre("game_events.log"));
  EXPECT_THAT(LinesOf("game_events.log"), ElementsAre(R"({"n":1})", R"({"n":2})"));
}

TEST_F(EventLogTest, OpeningCreatesTheDirectoryAndWritesNothingUntilAnEventDoes) {
  auto log = EventLog::Open(Dir(), "game_events");
  ASSERT_TRUE(log.ok()) << log.status();
  EXPECT_TRUE(std::filesystem::is_directory(dir_));
  EXPECT_THAT(LinesOf("game_events.log"), IsEmpty());
}

TEST_F(EventLogTest, OpeningUnderAPathThatIsNotADirectoryFails) {
  std::filesystem::create_directories(dir_);
  std::ofstream(dir_ / "blocked") << "not a directory";
  EXPECT_FALSE(EventLog::Open((dir_ / "blocked").string(), "game_events").ok());
}

}  // namespace
}  // namespace event_log
