#include "domains/games/apis/one_d4_worker/result.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace one_d4_worker {
namespace {

constexpr std::array<std::string_view, 7> kDraws = {
    "agreed", "repetition", "stalemate", "insufficient", "50move", "timevsinsufficient", "drawn"};

constexpr std::array<std::string_view, 5> kLosses = {"resigned", "checkmated", "timeout",
                                                     "abandoned", "lose"};

template <typename Words>
bool Holds(const Words& words, std::string_view value) {
  return std::find(words.begin(), words.end(), value) != words.end();
}

}  // namespace

std::string_view ResultOf(std::string_view white, std::string_view black) {
  if (white == "win") return "1-0";
  if (black == "win") return "0-1";
  if (Holds(kDraws, white) || Holds(kDraws, black)) return "1/2-1/2";
  if (Holds(kLosses, white)) return "0-1";
  if (Holds(kLosses, black)) return "1-0";
  return "unknown";
}

absl::Span<const std::string_view> KnownDraws() { return kDraws; }
absl::Span<const std::string_view> KnownLosses() { return kLosses; }

}  // namespace one_d4_worker
