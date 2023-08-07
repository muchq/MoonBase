#include "cpp/so_leet/so_leet.h"
#include <gtest/gtest.h>

#include <vector>

using namespace so_leet;


TEST(SoLeet, RunningSum) {
  std::vector<int> input{1, 2, 3, 4};

  std::vector<int> output = running_sum(input);
  std::vector<int> expected{1, 3, 6, 10};

  EXPECT_EQ(output, expected);
}
