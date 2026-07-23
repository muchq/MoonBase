#include "domains/games/libs/cards/golf/game_state.h"

#include <gtest/gtest.h>

#include <deque>
#include <unordered_set>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/player.h"

using namespace cards;
using namespace golf;

TEST(GameState, IsOver) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  Player p2{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> emptyDrawPile;
  std::deque<Card> nonEmptyDrawPile{Card{Suit::Clubs, Rank::Ace}};
  std::deque<Card> emptyDiscardPile;
  std::vector<Player> players{p1, p2};

  GameState g1{emptyDrawPile, emptyDiscardPile, players, false, 0, -1, "foo", "bar"};
  EXPECT_TRUE(g1.isOver());  // game is over when draw pile is empty

  GameState g2{nonEmptyDrawPile, emptyDiscardPile, players, false, 0, -1, "foo", "bar"};
  EXPECT_FALSE(g2.isOver());  // no one knocked and there's still a card on the draw pile

  GameState g3{nonEmptyDrawPile, emptyDiscardPile, players, false, 1, 1, "foo", "bar"};
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

  GameState g1{emptyDrawPile, emptyDiscardPile, players, false, 0, -1, "foo", "bar"};
  std::unordered_set<int> expectedWinnersG1{1, 0};
  EXPECT_TRUE(g1.isOver());  // game is over when draw pile is empty
  EXPECT_EQ(expectedWinnersG1, g1.winners());

  GameState g2{nonEmptyDrawPile, emptyDiscardPile, players, false, 1, 1, "foo", "bar"};
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
  mutableDrawPile.emplace_back(Suit::Diamonds, Rank::Jack);
  mutableDrawPile.emplace_back(Suit::Clubs, Rank::Ace);
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // p1 has drawn (peeked at the pile top) and swaps it for their top left
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, true, 1, -1, "foo", "bar"};
  auto g2 = g1.swapForDrawPile(1, Position::TopLeft);
  EXPECT_TRUE(g2.ok());

  GameState updatedState = *g2;

  // game should not be over yet
  EXPECT_FALSE(updatedState.isOver());

  // check draw pile
  const std::deque<Card> expectedDrawPile{Card{Suit::Diamonds, Rank::Jack}};
  EXPECT_EQ(updatedState.getDrawPile(), expectedDrawPile);

  // check discard pile
  const std::deque<Card> expectedDiscardPile{Card{Suit::Clubs, Rank::Three}};
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

  // check game id
  EXPECT_EQ(updatedState.getGameId(), g1.getGameId());
}

TEST(GameState, SwapForDrawPileFailsWhenGameIsOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  std::deque<Card> mutableDrawPile{};
  mutableDrawPile.emplace_back(Suit::Diamonds, Rank::Jack);
  mutableDrawPile.emplace_back(Suit::Clubs, Rank::Ace);
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // should not work because game is over
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, false, 1, 1, "foo", "bar"};
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
  mutableDrawPile.emplace_back(Suit::Diamonds, Rank::Jack);
  mutableDrawPile.emplace_back(Suit::Clubs, Rank::Ace);
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> emptyDiscardPile;
  const std::vector<Player> players{p0, p1};

  // should not work because it's player 0's turn
  const GameState g1{nonEmptyDrawPile, emptyDiscardPile, players, false, 0, -1, "foo", "bar"};
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
  mutableDrawPile.emplace_back(Suit::Diamonds, Rank::Jack);
  mutableDrawPile.emplace_back(Suit::Clubs, Rank::Ace);
  const std::deque<Card> drawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Queen}};
  const std::vector<Player> players{p0, p1};

  // should swap p1's top left card for Queen of Hearts
  const GameState g1{drawPile, discardPile, players, false, 1, -1, "foo", "bar"};
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
  const std::deque<Card> expectedDiscardPile{Card{Suit::Clubs, Rank::Three}};
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

  // game id is unchanged
  EXPECT_EQ(updatedState.getGameId(), g1.getGameId());

  // version id is unchanged
  EXPECT_EQ(updatedState.getVersionId(), g1.getVersionId());
}

TEST(GameState, SwapForDiscardPileFailsWhenGameIsOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Five}};
  const std::vector<Player> players{p0, p1};

  // should not work because game is over
  const GameState g1{drawPile, discardPile, players, false, 0, -1, "foo", "bar"};
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
  mutableDrawPile.emplace_back(Suit::Diamonds, Rank::Jack);
  mutableDrawPile.emplace_back(Suit::Clubs, Rank::Ace);
  const std::deque<Card> nonEmptyDrawPile = std::move(mutableDrawPile);
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const std::vector<Player> players{p0, p1};

  // should not work because it's player 0's turn
  const GameState g1{nonEmptyDrawPile, discardPile, players, false, 0, -1, "foo", "bar"};
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
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, false, 0, -1, "foo", "bar"};
  auto g2 = g1.knock(0);
  EXPECT_TRUE(g2.ok());

  EXPECT_EQ(g2->getDrawPile(), drawPile);
  EXPECT_EQ(g2->getDiscardPile(), discardPile);
  EXPECT_EQ(g2->getPlayers(), players);
  EXPECT_EQ(g2->getWhoseTurn(), 1);
  EXPECT_EQ(g2->getWhoKnocked(), 0);
  EXPECT_EQ(g2->getGameId(), g1.getGameId());
  EXPECT_EQ(g2->getVersionId(), g1.getVersionId());

  EXPECT_FALSE(g2->isOver());
}

TEST(GameState, KnockIsNotAllowedOnGameOver) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};

  const std::deque<Card> drawPile{};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, false, 0, -1, "foo", "bar"};
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
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, false, 1, -1, "foo", "bar"};
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
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const std::vector<Player> players{p0, p1};

  const GameState g1{drawPile, discardPile, players, false, 1, 0, "foo", "bar"};
  auto g2 = g1.knock(1);
  EXPECT_FALSE(g2.ok());
  EXPECT_EQ(g2.status().message(), "someone already knocked");
}

// The opening reveal (#1187 phase 2): two own-card peeks per player, a
// table-wide countdown once everyone is done, hideCards to end it.

namespace {

GameState freshTwoPlayerGame() {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};
  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}, Card{Suit::Clubs, Rank::Ace}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  return GameState{drawPile, discardPile, {p0, p1}, false, 0, -1, "foo", "bar"};
}

GameState allPeeked(const GameState& game) {
  // StatusOr<GameState> is not assignable (const members), so chain
  // through fresh values.
  auto first = game.peekOwnCard(0, Position::TopLeft);
  EXPECT_TRUE(first.ok()) << first.status();
  auto second = first->peekOwnCard(0, Position::TopRight);
  EXPECT_TRUE(second.ok()) << second.status();
  auto third = second->peekOwnCard(1, Position::BottomLeft);
  EXPECT_TRUE(third.ok()) << third.status();
  auto fourth = third->peekOwnCard(1, Position::BottomRight);
  EXPECT_TRUE(fourth.ok()) << fourth.status();
  return *fourth;
}

}  // namespace

TEST(GameState, OpeningPeeksAreCappedAndDupFree) {
  const GameState game = freshTwoPlayerGame();

  auto one = game.peekOwnCard(0, Position::TopLeft);
  ASSERT_TRUE(one.ok());
  EXPECT_EQ(one->getPlayer(0).getPeeked().size(), 1);
  EXPECT_FALSE(one->getPlayer(0).hasCompletedPeeks());
  EXPECT_FALSE(one->allPlayersPeeked());

  auto dup = one->peekOwnCard(0, Position::TopLeft);
  EXPECT_FALSE(dup.ok());
  EXPECT_EQ(dup.status().message(), "already peeked at this card");

  auto two = one->peekOwnCard(0, Position::BottomRight);
  ASSERT_TRUE(two.ok());
  EXPECT_TRUE(two->getPlayer(0).hasCompletedPeeks());

  auto three = two->peekOwnCard(0, Position::TopRight);
  EXPECT_FALSE(three.ok());
  EXPECT_EQ(three.status().message(), "already peeked at 2 cards");
}

TEST(GameState, CountdownGatesTurnMovesUntilHidden) {
  const GameState peeked = allPeeked(freshTwoPlayerGame());
  EXPECT_TRUE(peeked.revealCountdownActive());

  EXPECT_FALSE(peeked.peekAtDrawPile(0).ok());
  EXPECT_FALSE(peeked.swapForDrawPile(0, Position::TopLeft).ok());
  EXPECT_FALSE(peeked.swapForDiscardPile(0, Position::TopLeft).ok());
  EXPECT_FALSE(peeked.knock(0).ok());

  auto hidden = peeked.hideCards(1);
  ASSERT_TRUE(hidden.ok());
  EXPECT_FALSE(hidden->revealCountdownActive());
  EXPECT_TRUE(hidden->getPlayer(0).getPeeked().empty());
  EXPECT_TRUE(hidden->getPlayer(1).getPeeked().empty());

  // Play proceeds, and the countdown cannot restart: peeks are done.
  EXPECT_TRUE(hidden->peekAtDrawPile(0).ok());
  auto repeek = hidden->peekOwnCard(0, Position::TopLeft);
  EXPECT_FALSE(repeek.ok());
  EXPECT_EQ(repeek.status().message(), "already peeked at 2 cards");
}

TEST(GameState, HideCardsRequiresAnActiveCountdown) {
  const GameState game = freshTwoPlayerGame();
  auto hidden = game.hideCards(0);
  EXPECT_FALSE(hidden.ok());
  EXPECT_EQ(hidden.status().message(), "no reveal countdown to end");
}

TEST(GameState, TurnPlayBeforePeeksFinishStillWorks) {
  // Go-hub parity: nothing forces the opening to complete before the
  // first player draws; the countdown only gates once everyone peeked.
  const GameState game = freshTwoPlayerGame();
  auto drawn = game.peekAtDrawPile(0);
  ASSERT_TRUE(drawn.ok());
  EXPECT_TRUE(drawn->getPeekedAtDrawPile());
}

TEST(GameState, NoPeeksAfterAKnock) {
  const GameState game = freshTwoPlayerGame();
  auto knocked = game.knock(0);
  ASSERT_TRUE(knocked.ok());
  auto peek = knocked->peekOwnCard(1, Position::TopLeft);
  EXPECT_FALSE(peek.ok());
  EXPECT_EQ(peek.status().message(), "cannot peek after a knock");
}

TEST(GameState, PeeksSurviveSwapsAndCountdownStateSurvivesMoves) {
  const GameState hidden = *allPeeked(freshTwoPlayerGame()).hideCards(0);

  // A full turn later, the opening stays over: no countdown reactivation.
  auto swapped = hidden.swapForDiscardPile(0, Position::TopLeft);
  ASSERT_TRUE(swapped.ok());
  EXPECT_TRUE(swapped->getPeeksHidden());
  EXPECT_FALSE(swapped->revealCountdownActive());
  EXPECT_TRUE(swapped->getPlayer(0).hasCompletedPeeks());
  EXPECT_TRUE(swapped->knock(1).ok());
}

TEST(GameState, RemovePlayerCompactsSeatsAndFixesTurn) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};
  const Player p2{"Kim", Card(Suit::Clubs, Rank::Four), Card(Suit::Diamonds, Rank::Four),
                  Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Four)};
  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};

  // Removing a seat before the current player pulls the turn index back.
  const GameState game{drawPile, discardPile, {p0, p1, p2}, false, 2, -1, "foo", "bar"};
  auto removed = game.removePlayer(0);
  ASSERT_TRUE(removed.ok());
  ASSERT_EQ(removed->getPlayers().size(), 2);
  EXPECT_EQ(removed->getWhoseTurn(), 1);
  EXPECT_EQ(*removed->getPlayer(1).getName(), "Kim");

  // Removing the current player leaves the turn on whoever slid into the
  // seat (wrapping at the end of the table).
  const GameState wrap{drawPile, discardPile, {p0, p1, p2}, false, 2, -1, "foo", "bar"};
  auto tail = wrap.removePlayer(2);
  ASSERT_TRUE(tail.ok());
  EXPECT_EQ(tail->getWhoseTurn(), 0);

  EXPECT_FALSE(game.removePlayer(7).ok());
}

TEST(GameState, RemoveKnockerVoidsTheKnock) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};
  const Player p2{"Kim", Card(Suit::Clubs, Rank::Four), Card(Suit::Diamonds, Rank::Four),
                  Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Four)};
  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};

  const GameState knocked{drawPile, discardPile, {p0, p1, p2}, false, 1, 0, "foo", "bar"};
  auto gone = knocked.removePlayer(0);
  ASSERT_TRUE(gone.ok());
  EXPECT_EQ(gone->getWhoKnocked(), -1);
  EXPECT_EQ(gone->getWhoseTurn(), 0);
  EXPECT_FALSE(gone->isOver());

  // Removing a seat before the knocker keeps the knock on the same player.
  const GameState knocked2{drawPile, discardPile, {p0, p1, p2}, false, 0, 2, "foo", "bar"};
  auto shifted = knocked2.removePlayer(1);
  ASSERT_TRUE(shifted.ok());
  EXPECT_EQ(shifted->getWhoKnocked(), 1);
  EXPECT_EQ(*shifted->getPlayer(1).getName(), "Kim");
}

TEST(GameState, RemoveToBelowTwoPlayersEndsTheGame) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};
  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};

  const GameState game{drawPile, discardPile, {p0, p1}, false, 0, -1, "foo", "bar"};
  auto lone = game.removePlayer(1);
  ASSERT_TRUE(lone.ok());
  EXPECT_TRUE(lone->isOver());
  const std::unordered_set<int> expected{0};
  EXPECT_EQ(lone->winners(), expected);
}

// Validation negatives for the corrected rules (#1187): no blind moves,
// one draw per turn, and misuse dies typed instead of undefined.

TEST(GameState, BlindMovesAreRejected) {
  const GameState game = freshTwoPlayerGame();

  // Neither the swap nor the discard works without drawing first.
  auto blind_swap = game.swapForDrawPile(0, Position::TopLeft);
  EXPECT_FALSE(blind_swap.ok());
  EXPECT_EQ(blind_swap.status().message(), "no drawn card to swap");

  auto blind_discard = game.swapDrawForDiscardPile(0);
  EXPECT_FALSE(blind_discard.ok());
  EXPECT_EQ(blind_discard.status().message(), "no drawn card to discard");

  // After a draw, both are legal moves.
  auto drawn = game.peekAtDrawPile(0);
  ASSERT_TRUE(drawn.ok());
  EXPECT_TRUE(drawn->swapForDrawPile(0, Position::TopLeft).ok());
  EXPECT_TRUE(drawn->swapDrawForDiscardPile(0).ok());
}

TEST(GameState, OneDrawPerTurn) {
  const GameState game = freshTwoPlayerGame();
  auto drawn = game.peekAtDrawPile(0);
  ASSERT_TRUE(drawn.ok());
  auto again = drawn->peekAtDrawPile(0);
  EXPECT_FALSE(again.ok());
  EXPECT_EQ(again.status().message(), "you can only peek once per turn");
}

TEST(GameState, NoDiscardTakeAfterDrawing) {
  const GameState game = freshTwoPlayerGame();
  auto drawn = game.peekAtDrawPile(0);
  ASSERT_TRUE(drawn.ok());
  auto take = drawn->swapForDiscardPile(0, Position::TopLeft);
  EXPECT_FALSE(take.ok());
  EXPECT_EQ(take.status().message(), "cannot swap for discard after peeking");
}

TEST(GameState, EmptyDiscardPileRejectsTheTake) {
  const Player p0{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                  Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const Player p1{"Mercy", Card(Suit::Clubs, Rank::Three), Card(Suit::Diamonds, Rank::Three),
                  Card(Suit::Hearts, Rank::Three), Card(Suit::Spades, Rank::Three)};
  const std::deque<Card> drawPile{Card{Suit::Diamonds, Rank::Ten}};
  const std::deque<Card> emptyDiscardPile;

  const GameState game{drawPile, emptyDiscardPile, {p0, p1}, false, 0, -1, "foo", "bar"};
  auto take = game.swapForDiscardPile(0, Position::TopLeft);
  EXPECT_FALSE(take.ok());
  EXPECT_EQ(take.status().message(), "discard pile is empty");
}

TEST(GameState, PeekValidationNegatives) {
  const GameState game = freshTwoPlayerGame();

  // Out-of-range seats are typed errors on every entry point.
  EXPECT_EQ(game.peekOwnCard(7, Position::TopLeft).status().message(), "no such player");
  EXPECT_EQ(game.hideCards(7).status().message(), "no such player");

  // No peeking once the game has ended.
  const std::deque<Card> emptyDrawPile;
  const std::deque<Card> discardPile{Card{Suit::Hearts, Rank::Four}};
  const GameState over{emptyDrawPile, discardPile, game.getPlayers(), false, 0, -1, "foo", "bar"};
  ASSERT_TRUE(over.isOver());
  EXPECT_EQ(over.peekOwnCard(0, Position::TopLeft).status().message(), "game is over");
}

TEST(GameState, CountdownGatesTheDrawnDiscardToo) {
  const GameState peeked = allPeeked(freshTwoPlayerGame());
  ASSERT_TRUE(peeked.revealCountdownActive());
  auto gated = peeked.swapDrawForDiscardPile(0);
  EXPECT_FALSE(gated.ok());
  EXPECT_EQ(gated.status().message(), "waiting for peeked cards to be hidden");

  // Once hidden, the countdown is spent: a second hide is a typed error.
  auto hidden = peeked.hideCards(0);
  ASSERT_TRUE(hidden.ok());
  auto again = hidden->hideCards(0);
  EXPECT_FALSE(again.ok());
  EXPECT_EQ(again.status().message(), "no reveal countdown to end");
}

TEST(GameState, DealGolfGameValidatesAndDeals) {
  std::deque<Card> deck;
  for (int i = 0; i < 52; i++) {
    deck.emplace_back(i);
  }

  EXPECT_FALSE(dealGolfGame("G", {"solo"}, deck).ok());
  EXPECT_FALSE(dealGolfGame("G", {"a", "b", "c", "d", "e"}, deck).ok());
  EXPECT_FALSE(dealGolfGame("G", {"a", "b"}, std::deque<Card>{Card{0}}).ok());

  auto dealt = dealGolfGame("G", {"alice", "bob"}, deck);
  ASSERT_TRUE(dealt.ok());
  EXPECT_EQ(dealt->getPlayers().size(), 2);
  EXPECT_EQ(*dealt->getPlayer(0).getName(), "alice");
  // Cards come off the back of the deck: alice gets 51..48.
  EXPECT_EQ(dealt->getPlayer(0).cardAt(Position::TopLeft), Card{51});
  EXPECT_EQ(dealt->getPlayer(1).cardAt(Position::TopLeft), Card{47});
  EXPECT_EQ(dealt->getDiscardPile().size(), 1);
  EXPECT_EQ(dealt->getDiscardPile().back(), Card{43});
  EXPECT_EQ(dealt->getDrawPile().size(), 52 - 8 - 1);
  EXPECT_EQ(dealt->getWhoseTurn(), 0);
  EXPECT_EQ(dealt->getWhoKnocked(), -1);
  EXPECT_FALSE(dealt->isOver());
  EXPECT_FALSE(dealt->getPeeksHidden());
}
