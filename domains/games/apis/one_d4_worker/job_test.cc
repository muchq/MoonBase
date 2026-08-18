#include "domains/games/apis/one_d4_worker/job.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;

std::vector<std::string> Months(std::string_view start, std::string_view end) {
  const auto job = IndexJob::Months(start, end);
  EXPECT_TRUE(job.ok()) << job.status();
  std::vector<std::string> months;
  for (const YearMonth month : *job) months.push_back(month.ToString());
  return months;
}

TEST(YearMonth, ReadsTheStoredSpelling) {
  const auto month = YearMonth::Parse("2026-08");
  ASSERT_TRUE(month.ok()) << month.status();
  EXPECT_EQ(month->year, 2026);
  EXPECT_EQ(month->month, 8);
  EXPECT_EQ(month->ToString(), "2026-08");
}

TEST(YearMonth, RejectsAnythingElse) {
  for (const std::string_view bad : {"2026-8", "2026-13", "2026-00", "202608", "", "not-a-month"}) {
    EXPECT_FALSE(YearMonth::Parse(bad).ok()) << bad;
  }
}

TEST(YearMonth, Orders) {
  EXPECT_TRUE(*YearMonth::Parse("2025-12") < *YearMonth::Parse("2026-01"));
  EXPECT_FALSE(*YearMonth::Parse("2026-01") < *YearMonth::Parse("2026-01"));
}

TEST(IndexJobMonths, WalksTheRangeInclusive) {
  EXPECT_THAT(Months("2026-01", "2026-03"), ElementsAre("2026-01", "2026-02", "2026-03"));
}

TEST(IndexJobMonths, CrossesTheYearBoundary) {
  EXPECT_THAT(Months("2025-11", "2026-02"),
              ElementsAre("2025-11", "2025-12", "2026-01", "2026-02"));
}

TEST(IndexJobMonths, IsOneMonthWhenBothEndsMatch) {
  EXPECT_THAT(Months("2026-08", "2026-08"), ElementsAre("2026-08"));
}

TEST(IndexJobMonths, RejectsARangeThatRunsBackwards) {
  // A request nobody can serve: the API validates this, and a worker that
  // silently indexed nothing would report success for it.
  EXPECT_FALSE(IndexJob::Months("2026-03", "2026-01").ok());
}

TEST(IndexJobMonths, RejectsAMonthItCannotRead) {
  EXPECT_FALSE(IndexJob::Months("2026-1", "2026-03").ok());
  EXPECT_FALSE(IndexJob::Months("2026-01", "").ok());
}

TEST(YearMonth, KnowsWhenAMonthBegins) {
  // The epoch instants the period rule compares against. Wrong by a day and
  // the current month is stored complete for its last day, or the previous
  // one is refetched forever.
  EXPECT_EQ((YearMonth{1970, 1}).FirstInstant(), 0);
  EXPECT_EQ((YearMonth{2026, 1}).FirstInstant(), 1767225600);
  EXPECT_EQ((YearMonth{2026, 9}).FirstInstant(), 1788220800);
  // March 1st of a leap year and of the year after: the day the shifted-year
  // arithmetic exists for.
  EXPECT_EQ((YearMonth{2024, 3}).FirstInstant(), 1709251200);
  EXPECT_EQ((YearMonth{2025, 3}).FirstInstant(), 1740787200);
  EXPECT_EQ((YearMonth{2000, 3}).FirstInstant(), 951868800);
  EXPECT_EQ((YearMonth{1900, 3}).FirstInstant(), -2203891200);
}

}  // namespace
}  // namespace one_d4_worker
