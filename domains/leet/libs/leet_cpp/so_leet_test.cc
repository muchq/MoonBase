#include "domains/leet/libs/leet_cpp/so_leet.h"

#include <gtest/gtest.h>

#include <vector>

using namespace so_leet;

TEST(SoLeet, RunningSum) {
  std::vector<int> input{1, 2, 3, 4};

  std::vector<int> output = running_sum(input);
  std::vector<int> expected{1, 3, 6, 10};

  EXPECT_EQ(output, expected);
}

TEST(SoLeet, BuyAndSellStock) {
  std::vector<int> prices{7, 1, 5, 3, 6, 4};
  int maxProfit = buy_and_sell_stock(prices);
  EXPECT_EQ(maxProfit, 5);
}
