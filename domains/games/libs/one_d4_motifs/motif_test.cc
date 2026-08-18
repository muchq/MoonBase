#include "domains/games/libs/one_d4_motifs/motif.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace one_d4 {
namespace {

// Every value, so a new motif cannot be added without naming it here.
constexpr Motif kAll[] = {
    Motif::kPin,
    Motif::kCrossPin,
    Motif::kFork,
    Motif::kSkewer,
    Motif::kAttack,
    Motif::kDiscoveredAttack,
    Motif::kDiscoveredCheck,
    Motif::kCheck,
    Motif::kCheckmate,
    Motif::kPromotion,
    Motif::kPromotionWithCheck,
    Motif::kPromotionWithCheckmate,
    Motif::kBackRankMate,
    Motif::kSmotheredMate,
    Motif::kZugzwang,
    Motif::kDoubleCheck,
    Motif::kOverloadedPiece,
};

TEST(Motif, NamesRoundTrip) {
  for (const Motif motif : kAll) {
    const std::string_view name = ToString(motif);
    EXPECT_NE(name, "UNKNOWN") << "a motif with no stored name";
    EXPECT_EQ(MotifFromString(name), motif);
  }
}

TEST(Motif, NamesAreTheStoredSpelling) {
  // What `motif_occurrences.motif` holds and what ChessQL matches on.
  EXPECT_EQ(ToString(Motif::kPin), "PIN");
  EXPECT_EQ(ToString(Motif::kPromotionWithCheckmate), "PROMOTION_WITH_CHECKMATE");
  EXPECT_EQ(ToString(Motif::kBackRankMate), "BACK_RANK_MATE");
}

TEST(Motif, RejectsANameItDoesNotKnow) {
  EXPECT_EQ(MotifFromString("SACRIFICE"), std::nullopt);
  EXPECT_EQ(MotifFromString("pin"), std::nullopt) << "stored names are upper case";
  EXPECT_EQ(MotifFromString(""), std::nullopt);
}

}  // namespace
}  // namespace one_d4
