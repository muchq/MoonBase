#include <gtest/gtest.h>

#include "src/main/cpp/example_cc_go/example_cc_go.h"

TEST(HelloTest, BasicAssertions) {
    EXPECT_EQ("Sup Buddy", example_cc_go::MakeGreeting("Buddy"));
}
