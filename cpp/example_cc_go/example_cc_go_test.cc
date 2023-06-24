#include "cpp/example_cc_go/example_cc_go.h"

#include <gtest/gtest.h>

TEST(HelloTest, BasicAssertions) { EXPECT_EQ("Sup Buddy", example_cc_go::MakeGreeting("Buddy")); }
