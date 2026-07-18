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

TEST(ReadListTest, EmptyWhenUnsetOrEmpty) {
  unsetenv("READ_LIST_TEST");
  EXPECT_TRUE(futility::env::ReadList("READ_LIST_TEST").empty());
  setenv("READ_LIST_TEST", "", 1);
  EXPECT_TRUE(futility::env::ReadList("READ_LIST_TEST").empty());
  unsetenv("READ_LIST_TEST");
}

TEST(ReadListTest, SplitsOnCommasTrimsSpacesDropsEmptyEntries) {
  setenv("READ_LIST_TEST", " 10.0.0.0/8, 172.28.0.2 ,,2600:1f00::/24", 1);
  EXPECT_EQ(futility::env::ReadList("READ_LIST_TEST"),
            (std::vector<std::string>{"10.0.0.0/8", "172.28.0.2", "2600:1f00::/24"}));
  unsetenv("READ_LIST_TEST");
}

}  // namespace
