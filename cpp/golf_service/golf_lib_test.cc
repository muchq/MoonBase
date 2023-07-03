#include "cpp/golf_service/golf_lib.h"

#include <gtest/gtest.h>

#include <iostream>

#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

TEST(GOLF_LIB_TEST, PlayerAssertions) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};  // all twos
  EXPECT_EQ(0, p1.score());

  Player p2{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Five)};
  EXPECT_EQ(14, p2.score());

  Player p3{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Jack), Card(Suit::Spades, Rank::Ace)};
  for (auto c : p3.allCards()) {
    std::cout << c.debug_string() << "\n";
  }
  EXPECT_EQ(1, p3.score());
}