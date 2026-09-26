// Production id shapes are contracts, not aesthetics: the UI's permalink
// validation admits alphanumeric ids and the lobby's room-code input
// upshifts exactly six characters. Shape only — values are random and
// collisions are legal (the hub rerolls), so neither is asserted.

#include "domains/games/apis/games_hub/id_generator.h"

#include <gtest/gtest.h>

#include <cctype>
#include <initializer_list>
#include <string>

#include "domains/games/apis/games_hub/chat_store.h"

namespace games_hub {
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

// The room bot posts as kBotPlayerId (#1591), and its replies are told
// apart by that id alone: no generator may mint it. Every minted id has
// a hyphen and the bot's has none.
TEST(IdGenerators, NeverMintTheBotsId) {
  ASSERT_EQ(std::string(kBotPlayerId).find('-'), std::string::npos);
  WhimsicalIdGenerator whimsical;
  SequentialIdGenerator sequential;
  RemoteIdGenerator remote;
  for (IdGenerator* ids : std::initializer_list<IdGenerator*>{&whimsical, &sequential, &remote}) {
    for (int i = 0; i < 100; ++i) {
      EXPECT_NE(ids->PlayerId().find('-'), std::string::npos);
    }
  }
}

}  // namespace
}  // namespace games_hub
