// Production id shapes are contracts, not aesthetics: the UI's permalink
// validation admits alphanumeric ids and the lobby's room-code input
// upshifts exactly six characters. Shape only — values are random and
// collisions are legal (the hub rerolls), so neither is asserted.

#include "domains/games/apis/golf_hub/id_generator.h"

#include <gtest/gtest.h>

#include <cctype>
#include <string>

namespace golf_hub {
namespace {

bool IsSixCharUppercaseAlnum(const std::string& id) {
  if (id.size() != 6) return false;
  for (const char c : id) {
    const auto uc = static_cast<unsigned char>(c);
    if (!std::isupper(uc) && !std::isdigit(uc)) return false;
  }
  return true;
}

TEST(WhimsicalIdGenerator, RoomAndGameCodesAreSixCharUppercaseAlnum) {
  WhimsicalIdGenerator ids;
  for (int i = 0; i < 200; ++i) {
    const std::string room = ids.RoomId();
    const std::string game = ids.GameCode();
    EXPECT_TRUE(IsSixCharUppercaseAlnum(room)) << room;
    EXPECT_TRUE(IsSixCharUppercaseAlnum(game)) << game;
  }
}

TEST(WhimsicalIdGenerator, PlayerIdIsLowercaseSlugWithThreeWords) {
  WhimsicalIdGenerator ids;
  for (int i = 0; i < 200; ++i) {
    const std::string id = ids.PlayerId();
    int hyphens = 0;
    for (const char c : id) {
      if (c == '-') {
        ++hyphens;
        continue;
      }
      const auto uc = static_cast<unsigned char>(c);
      EXPECT_TRUE(std::islower(uc) || std::isdigit(uc)) << id;
    }
    EXPECT_EQ(hyphens, 3) << id;
    // adjective-color-animal-xxxx: the final segment is the 4-char slug.
    EXPECT_EQ(id.rfind('-'), id.size() - 5) << id;
  }
}

}  // namespace
}  // namespace golf_hub
