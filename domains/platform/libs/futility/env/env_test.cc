#include "domains/platform/libs/futility/env/env.h"

#include <gtest/gtest.h>

#include <cstdlib>

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

}  // namespace
