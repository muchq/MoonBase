#include "domains/platform/libs/futility/env/env.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

TEST(ReadPortTest, DefaultWhenUnset) {
  unsetenv("PORT");
  EXPECT_EQ(futility::env::ReadPort(8080), 8080);
}

TEST(ReadPortTest, ReadsPortFromEnvironment) {
  setenv("PORT", "9000", 1);
  EXPECT_EQ(futility::env::ReadPort(8080), 9000);
  unsetenv("PORT");
}

TEST(ReadListTest, EmptyWhenUnsetOrEmptyOrWhitespaceOnly) {
  unsetenv("READ_LIST_TEST");
  EXPECT_TRUE(futility::env::ReadList("READ_LIST_TEST").empty());
  setenv("READ_LIST_TEST", "", 1);
  EXPECT_TRUE(futility::env::ReadList("READ_LIST_TEST").empty());
  setenv("READ_LIST_TEST", " ", 1);
  EXPECT_TRUE(futility::env::ReadList("READ_LIST_TEST").empty());
  unsetenv("READ_LIST_TEST");
}

TEST(ReadListTest, SplitsOnCommasTrimsSpacesDropsEmptyEntries) {
  setenv("READ_LIST_TEST", " 10.0.0.0/8, 172.28.0.2 ,,2600:1f00::/24", 1);
  EXPECT_EQ(futility::env::ReadList("READ_LIST_TEST"),
            (std::vector<std::string>{"10.0.0.0/8", "172.28.0.2", "2600:1f00::/24"}));
  unsetenv("READ_LIST_TEST");
}

// The compose-typo shape: a trailing comma must yield the entry alone, not a
// trailing "" that TrustedProxies would reject at startup.
TEST(ReadListTest, TrailingCommaDropsTheEmptyTail) {
  setenv("READ_LIST_TEST", "172.28.0.2,", 1);
  EXPECT_EQ(futility::env::ReadList("READ_LIST_TEST"), (std::vector<std::string>{"172.28.0.2"}));
  unsetenv("READ_LIST_TEST");
}

TEST(ReadPositiveSeconds, ReadsAWholeNumberOfSeconds) {
  setenv("FUTILITY_TEST_SECONDS", "30", 1);
  EXPECT_EQ(futility::env::ReadPositiveSeconds("FUTILITY_TEST_SECONDS"), 30);
  unsetenv("FUTILITY_TEST_SECONDS");
}

TEST(ReadPositiveSeconds, HasNoAnswerWhenItIsUnsetOrEmpty) {
  unsetenv("FUTILITY_TEST_SECONDS");
  EXPECT_EQ(futility::env::ReadPositiveSeconds("FUTILITY_TEST_SECONDS"), std::nullopt);

  setenv("FUTILITY_TEST_SECONDS", "", 1);
  EXPECT_EQ(futility::env::ReadPositiveSeconds("FUTILITY_TEST_SECONDS"), std::nullopt);
  unsetenv("FUTILITY_TEST_SECONDS");
}

TEST(ReadPositiveSeconds, RefusesWhatItCannotRead) {
  // atoi answers 0 for every one of these, which is how a typo becomes a
  // tight loop or a missing timeout rather than an error.
  for (const char* value : {"thirty", "30s", "", " ", "3.5", "0x1e"}) {
    setenv("FUTILITY_TEST_SECONDS", value, 1);
    EXPECT_EQ(futility::env::ReadPositiveSeconds("FUTILITY_TEST_SECONDS"), std::nullopt)
        << "read " << value << " as a number of seconds";
  }
  unsetenv("FUTILITY_TEST_SECONDS");
}

TEST(ReadPositiveSeconds, RefusesZeroAndNegative) {
  for (const char* value : {"0", "-1", "-30"}) {
    setenv("FUTILITY_TEST_SECONDS", value, 1);
    EXPECT_EQ(futility::env::ReadPositiveSeconds("FUTILITY_TEST_SECONDS"), std::nullopt)
        << value << " was honoured as an interval";
  }
  unsetenv("FUTILITY_TEST_SECONDS");
}

}  // namespace
