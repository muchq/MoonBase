#include "cpp/golf_service/game_state_mapper.h"

#include <gtest/gtest.h>

#include "cpp/cards/card.h"
#include "cpp/cards/golf/player.h"
#include "protos/golf_ws/golf_ws.pb.h"

using namespace cards;
using namespace golf;

TEST(GameStateMapper, GameStateToProto) {
  CardMapper cm;
  GameStateMapper gsm{cm};
  std::deque<Card> drawPile{Card{5}};     // 3D
  std::deque<Card> discardPile{Card{6}};  // 3H
  std::vector<Player> players{{"andy", Card{0}, Card{1}, Card{2}, Card{3}}};

  GameStatePtr state = std::make_shared<GameState>(
      GameState{drawPile, discardPile, players, false, 0, -1, "foo", "bar"});

  auto proto = gsm.gameStateToProto(state, "andy");

  EXPECT_TRUE(proto.all_here());
  EXPECT_EQ(proto.discard_size(), 1);
  EXPECT_EQ(proto.draw_size(), 1);
  EXPECT_EQ(proto.game_id(), "foo");
  EXPECT_FALSE(proto.game_over());
  EXPECT_TRUE(proto.has_hand());

  const auto &hand = proto.hand();
  EXPECT_EQ(hand.bottom_left(), "2_H");
  EXPECT_EQ(hand.bottom_right(), "2_S");

  EXPECT_FALSE(proto.has_knocker());
  EXPECT_EQ(proto.number_of_players(), 1);
  EXPECT_TRUE(proto.has_top_discard());
  EXPECT_EQ(proto.top_discard(), "3_H");
  EXPECT_FALSE(proto.has_top_draw());
  EXPECT_TRUE(proto.your_turn());
}
