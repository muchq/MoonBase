#include "domains/games/libs/one_d4_motifs/motif.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace one_d4 {
namespace {

// One table, so a rename cannot go one way only.
constexpr std::array<std::pair<Motif, std::string_view>, 17> kNames = {{
    {Motif::kPin, "PIN"},
    {Motif::kCrossPin, "CROSS_PIN"},
    {Motif::kFork, "FORK"},
    {Motif::kSkewer, "SKEWER"},
    {Motif::kAttack, "ATTACK"},
    {Motif::kDiscoveredAttack, "DISCOVERED_ATTACK"},
    {Motif::kDiscoveredCheck, "DISCOVERED_CHECK"},
    {Motif::kCheck, "CHECK"},
    {Motif::kCheckmate, "CHECKMATE"},
    {Motif::kPromotion, "PROMOTION"},
    {Motif::kPromotionWithCheck, "PROMOTION_WITH_CHECK"},
    {Motif::kPromotionWithCheckmate, "PROMOTION_WITH_CHECKMATE"},
    {Motif::kBackRankMate, "BACK_RANK_MATE"},
    {Motif::kSmotheredMate, "SMOTHERED_MATE"},
    {Motif::kZugzwang, "ZUGZWANG"},
    {Motif::kDoubleCheck, "DOUBLE_CHECK"},
    {Motif::kOverloadedPiece, "OVERLOADED_PIECE"},
}};

}  // namespace

std::string_view ToString(Motif motif) {
  for (const auto& [value, name] : kNames) {
    if (value == motif) return name;
  }
  return "UNKNOWN";
}

std::optional<Motif> MotifFromString(std::string_view name) {
  for (const auto& [value, candidate] : kNames) {
    if (candidate == name) return value;
  }
  return std::nullopt;
}

}  // namespace one_d4
