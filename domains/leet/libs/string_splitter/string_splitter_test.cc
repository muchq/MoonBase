extern "C" {
#include "string_splitter.h"
}

#include <gtest/gtest.h>

TEST(StringSplitter, NegativeTests) {
    EXPECT_EQ(nullptr, new_split_string_holder(nullptr, ","));
    EXPECT_EQ(nullptr, new_split_string_holder("hello,world,2", nullptr));
}
