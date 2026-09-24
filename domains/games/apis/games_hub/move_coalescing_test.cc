// MoveCoalescing: a walker's moves to one reader share a coalesce key;
// every other update to that reader is reliable and changes the key.

#include "domains/games/apis/games_hub/move_coalescing.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace games_hub {
namespace {

using moonbase::games::LobbyUpdate;
using opal::server::DeliveryClass;

LobbyUpdate Moved(const std::string& player_id) {
  moonbase::games::PlayerMoved moved;
  moved.playerId = player_id;
  return LobbyUpdate::FromPlayermoved(std::move(moved));
}

std::vector<LobbyUpdate> EverythingButAMove() {
  moonbase::games::PlayerLeft left;
  left.playerId = "carol";
  moonbase::games::ShapeChanged shape;
  shape.playerId = "carol";
  return {LobbyUpdate::FromPlayerleft(std::move(left)),
          LobbyUpdate::FromShapechanged(std::move(shape)),
          LobbyUpdate::FromPlayerjoined({}),
          LobbyUpdate::FromWorldstate({}),
          LobbyUpdate::FromGeometrychanged({}),
          LobbyUpdate::FromTape({})};
}

TEST(MoveCoalescingTest, AWalkersMovesShareAKeyAndNoOneElsesDo) {
  MoveCoalescing coalescing;
  const DeliveryClass first = coalescing.For("reader", Moved("alice"));
  const DeliveryClass again = coalescing.For("reader", Moved("alice"));
  const DeliveryClass bob = coalescing.For("reader", Moved("bob"));
  ASSERT_EQ(first.kind, DeliveryClass::Kind::kCoalesce);
  EXPECT_EQ(first.key, again.key);
  EXPECT_NE(first.key, bob.key);
}

// A move after any other update to the reader gets a new key, so it never
// replaces a move queued ahead of that update.
TEST(MoveCoalescingTest, AnythingElseSentToTheReaderIsReliableAndStartsAfresh) {
  for (const LobbyUpdate& between : EverythingButAMove()) {
    MoveCoalescing coalescing;
    const DeliveryClass before = coalescing.For("reader", Moved("alice"));
    EXPECT_EQ(coalescing.For("reader", between).kind, DeliveryClass::Kind::kReliable)
        << between.case_name();
    EXPECT_NE(coalescing.For("reader", Moved("alice")).key, before.key) << between.case_name();
  }
}

// Updates to other readers leave this reader's key unchanged.
TEST(MoveCoalescingTest, WhatOthersAreSentLeavesThisReadersMovesFolding) {
  MoveCoalescing coalescing;
  const DeliveryClass before = coalescing.For("reader", Moved("alice"));
  for (const LobbyUpdate& elsewhere : EverythingButAMove()) {
    (void)coalescing.For("someone else", elsewhere);
  }
  EXPECT_EQ(coalescing.For("reader", Moved("alice")).key, before.key);
}

// Forget drops the reader; its next snapshot starts a fresh generation.
TEST(MoveCoalescingTest, AForgottenReaderIsDroppedAndComesBackOnAFreshGeneration) {
  MoveCoalescing coalescing;
  (void)coalescing.For("reader", LobbyUpdate::FromWorldstate({}));
  const DeliveryClass earlier = coalescing.For("reader", Moved("alice"));
  ASSERT_EQ(coalescing.readers(), 1u);
  coalescing.Forget("reader");
  EXPECT_EQ(coalescing.readers(), 0u);
  (void)coalescing.For("reader", LobbyUpdate::FromWorldstate({}));
  EXPECT_NE(coalescing.For("reader", Moved("alice")).key, earlier.key);
}

}  // namespace
}  // namespace games_hub
