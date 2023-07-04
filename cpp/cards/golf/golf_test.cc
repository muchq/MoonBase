#include "cpp/cards/golf/golf.h"

#include <gtest/gtest.h>

#include <deque>
#include <iostream>
#include <vector>

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

TEST(GOLF_LIB_TEST, GameOverAssertions) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  Player p2{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> emptyDrawPile;
  std::deque<Card> nonEmptyDrawPile{Card{Suit::Clubs, Rank::Ace}};
  std::deque<Card> emptyDiscardPile;
  std::vector<Player> players{p1, p2};

  GameState g1{emptyDrawPile, emptyDiscardPile, players, 0, -1};
  EXPECT_TRUE(g1.isOver());  // game is over when draw pile is empty

  GameState g2{nonEmptyDrawPile, emptyDiscardPile, players, 0, -1};
  EXPECT_FALSE(g2.isOver());  // no one knocked and there's still a card on the draw pile

  GameState g3{nonEmptyDrawPile, emptyDiscardPile, players, 1, 1};
  EXPECT_TRUE(g3.isOver());  // player 1 knocked and it's their turn again
}

TEST(GOLF_LIB_TEST, WinnerAssertions) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  Player p2{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> emptyDrawPile;
  std::deque<Card> nonEmptyDrawPile{Card{Suit::Clubs, Rank::Ace}};
  std::deque<Card> emptyDiscardPile;
  std::vector<Player> players{p1, p2};

  GameState g1{emptyDrawPile, emptyDiscardPile, players, 0, -1};
  std::unordered_set<int> expectedWinnersG1{1, 0};
  EXPECT_TRUE(g1.isOver());  // game is over when draw pile is empty
  EXPECT_EQ(expectedWinnersG1, g1.winners());

  GameState g2{nonEmptyDrawPile, emptyDiscardPile, players, 1, 1};
  std::unordered_set<int> expectedWinnersG2{1};  // tie goes to the runner
  EXPECT_TRUE(g2.isOver());  // game is over because player 1 knocked and it's their turn again
  EXPECT_EQ(expectedWinnersG2, g2.winners());
}

TEST(GOLF_LIB_TEST, GameStateDrawPileAssertions) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  Player p2{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> nonEmptyDrawPile{Card{Suit::Clubs, Rank::Ace}};
  std::deque<Card> emptyDiscardPile;
  std::vector<Player> players{p1, p2};

  GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, 1, 1};
  auto g2 = g1.swapForDiscardPile(1, Position::TopLeft);
}
