#include "domains/leet/libs/leet_cpp/so_leet.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <vector>

namespace so_leet {
std::vector<int> running_sum(std::vector<int>& nums) {
  std::vector<int> output;
  std::inclusive_scan(nums.cbegin(), nums.cend(), std::back_inserter(output), std::plus<>());
  return output;
}

int buy_and_sell_stock(std::vector<int>& prices) {
  int minSoFar = std::numeric_limits<int>::max();
  int bestProfit = 0;
  for (auto price : prices) {
    minSoFar = std::min(minSoFar, price);
    bestProfit = std::max(bestProfit, price - minSoFar);
  }
  return bestProfit;
}

}  // namespace so_leet
