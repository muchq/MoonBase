#include "cpp/so_leet/so_leet.h"

#include <functional>
#include <iterator>
#include <numeric>
#include <vector>

namespace so_leet {
std::vector<int> running_sum(std::vector<int>& nums) {
  std::vector<int> output;
  std::inclusive_scan(nums.cbegin(), nums.cend(), std::back_inserter(output), std::plus<>());
  return output;
}
}  // namespace so_leet
