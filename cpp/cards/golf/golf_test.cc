#include "cpp/cards/golf/golf.h"

#include <gtest/gtest.h>

#include <deque>
#include <iostream>
#include <vector>

#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

TEST(Player, Score) {
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

TEST(GameState, IsOver) {
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

TEST(GameState, Winners) {
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

TEST(GameState, SwapForDrawPile) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.push_back(Card{Suit::Diamonds, Rank::Jack});
  mutableDrawPile.push_back(Card{Suit::Clubs, Rank::Ace});
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // should swap p1's top left card for Ace of Clubs
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, 1, -1};
  auto g2 = g1.swapForDrawPile(1, Position::TopLeft);
  EXPECT_TRUE(g2.ok());

  GameState updatedState = *g2;

  // game should not be over yet
  EXPECT_FALSE(updatedState.isOver());

  // check draw pile
  const std::deque<Card> expectedDrawPile{Card{Suit::Diamonds, Rank::Jack}};
  EXPECT_EQ(updatedState.getDrawPile(), expectedDrawPile);

  // check discard pile
  const std::deque<Card> expectedDiscardPile{Card{Suit::Clubs, Rank::Three, Facing::Up}};
  EXPECT_EQ(updatedState.getDiscardPile(), expectedDiscardPile);

  // check players
  EXPECT_EQ(updatedState.getPlayers().at(0), p0);

  const Player updatedP1{"Mercy", Card{Suit::Clubs, Rank::Ace}, Card{Suit::Diamonds, Rank::Three},
                         Card{Suit::Hearts, Rank::Three}, Card{Suit::Spades, Rank::Three}};
  EXPECT_EQ(updatedState.getPlayers().at(1), updatedP1);

  // check whose turn
  EXPECT_EQ(updatedState.getWhoseTurn(), 0);

  // check who knocked
  EXPECT_EQ(updatedState.getWhoKnocked(), -1);
}

TEST(GameState, SwapForDrawPileFailsWhenGameIsOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.push_back(Card{Suit::Diamonds, Rank::Jack});
  mutableDrawPile.push_back(Card{Suit::Clubs, Rank::Ace});
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // should not work because game is over
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, 1, 1};
  auto g2 = g1.swapForDrawPile(1, Position::TopLeft);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "game is over");
}

TEST(GameState, SwapForDrawPileFailsWhenNotYourTurn) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.push_back(Card{Suit::Diamonds, Rank::Jack});
  mutableDrawPile.push_back(Card{Suit::Clubs, Rank::Ace});
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // should not work because it's player 0's turn
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, 0, -1};
  auto g2 = g1.swapForDrawPile(1, Position::TopLeft);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "not your turn");
}

TEST(GameState, SwapForDiscardPile) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.push_back(Card{Suit::Diamonds, Rank::Jack});
  mutableDrawPile.push_back(Card{Suit::Clubs, Rank::Ace});
  const std::deque<Card> drawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Queen, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  // should swap p1's top left card for Queen of Hearts
  const GameState g1{drawPile, discardPile, players, 1, -1};
  auto g2 = g1.swapForDiscardPile(1, Position::TopLeft);
  EXPECT_TRUE(g2.ok());

  GameState updatedState = *g2;

  // game should not be over yet
  EXPECT_FALSE(updatedState.isOver());

  // check draw pile
  const std::deque<Card> expectedDrawPile{Card{Suit::Diamonds, Rank::Jack},
                                          Card{Suit::Clubs, Rank::Ace}};
  EXPECT_EQ(updatedState.getDrawPile(), expectedDrawPile);

  // check discard pile
  const std::deque<Card> expectedDiscardPile{Card{Suit::Clubs, Rank::Three, Facing::Up}};
  EXPECT_EQ(updatedState.getDiscardPile(), expectedDiscardPile);

  // check players
  EXPECT_EQ(updatedState.getPlayers().at(0), p0);

  const Player updatedP1{"Mercy", Card{Suit::Hearts, Rank::Queen},
                         Card{Suit::Diamonds, Rank::Three}, Card{Suit::Hearts, Rank::Three},
                         Card{Suit::Spades, Rank::Three}};
  EXPECT_EQ(updatedState.getPlayers().at(1), updatedP1);

  // check whose turn
  EXPECT_EQ(updatedState.getWhoseTurn(), 0);

  // check who knocked
  EXPECT_EQ(updatedState.getWhoKnocked(), -1);
}

TEST(GameState, SwapForDiscardPileFailsWhenGameIsOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Five, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  // should not work because game is over
  const GameState g1{drawPile, discardPile, players, 0, -1};
  auto g2 = g1.swapForDiscardPile(1, Position::TopLeft);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "game is over");
}

TEST(GameState, SwapForDiscardPileFailsWhenNotYourTurn) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.push_back(Card{Suit::Diamonds, Rank::Jack});
  mutableDrawPile.push_back(Card{Suit::Clubs, Rank::Ace});
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  // should not work because it's player 0's turn
  const GameState g1{nonEmptyDrawPile, discardPile, players, 0, -1};
  auto g2 = g1.swapForDrawPile(1, Position::TopLeft);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "not your turn");
}

TEST(GameState, Knock) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, 0, -1};
  auto g2 = g1.knock(0);
  EXPECT_TRUE(g2.ok());

  EXPECT_EQ(g2->getDrawPile(), drawPile);
  EXPECT_EQ(g2->getDiscardPile(), discardPile);
  EXPECT_EQ(g2->getPlayers(), players);
  EXPECT_EQ(g2->getWhoseTurn(), 1);
  EXPECT_EQ(g2->getWhoKnocked(), 0);

  EXPECT_FALSE(g2->isOver());
}

TEST(GameState, KnockIsNotAllowedOnGameOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, 0, -1};
  auto g2 = g1.knock(0);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "game is over");
}

TEST(GameState, KnockIsNotAllowedIfNotYourTurn) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, 1, -1};
  auto g2 = g1.knock(0);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "not your turn");
}

TEST(GameState, KnockIsOnlyAllowedOnce) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four, Facing::Up}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, 1, 0};
  auto g2 = g1.knock(1);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "someone already knocked");
}
