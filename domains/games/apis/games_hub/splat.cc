#include "domains/games/apis/games_hub/splat.h"

#include <cstdint>

namespace games_hub {
namespace {

/// SplitMix64's finalizer. A bijection over 64 bits whose every output bit
/// depends on every input bit, which is what makes seq 7 and seq 8 land on
/// opposite walls instead of a hand's breadth apart.
std::uint64_t Mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

/// 21 bits as a fraction in [0, 1). The divisor is 2^21, so the largest
/// field is 1 - 2^-21 and never reaches the wall's far edge.
constexpr std::uint64_t kFractionMask = (1ULL << 21) - 1;
constexpr double kFractionScale = 1.0 / static_cast<double>(1ULL << 21);

}  // namespace

Splat SplatFor(std::int64_t seq) {
  const std::uint64_t mixed = Mix(static_cast<std::uint64_t>(seq));
  Splat splat;
  splat.wall = static_cast<std::int32_t>(mixed % static_cast<std::uint64_t>(kGlassWalls));
  splat.u = static_cast<double>((mixed >> 11) & kFractionMask) * kFractionScale;
  splat.v = static_cast<double>((mixed >> 40) & kFractionMask) * kFractionScale;
  return splat;
}

}  // namespace games_hub
