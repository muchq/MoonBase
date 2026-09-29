#include "domains/games/libs/cards/rummy/gin.h"

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"

using namespace cards;
using namespace rummy;
using std::string;
using std::vector;

namespace {

Card c(Rank rank, Suit suit) { return Card{suit, rank}; }

// A deal in play at seat `turn`'s `stage`, alice in seat 0 and bob in 1.
GinState playing(vector<Card> alice, vector<Card> bob, std::deque<Card> stock, vector<Card> discard,
                 int turn, GinStage stage, std::optional<Card> taken = std::nullopt) {
  return GinState{std::move(stock),
                  std::move(discard),
                  {{"alice", std::move(alice)}, {"bob", std::move(bob)}},
                  turn,
                  stage,
                  Phase::Playing,
                  0,
                  taken,
                  std::nullopt,
                  std::nullopt,
                  "g",
                  ""};
}

std::deque<Card> someStock(int n) {
  std::deque<Card> stock;
  for (int i = 0; i < n; i++) stock.emplace_back(i);
  return stock;
}

// Alice's knocking hand after her draw: runs and a set with 2♦ and 9♦ over.
vector<Card> knocker() {
  return {
      c(Rank::Three, Suit::Hearts), c(Rank::Four, Suit::Hearts),    c(Rank::Five, Suit::Hearts),
      c(Rank::Seven, Suit::Clubs),  c(Rank::Seven, Suit::Diamonds), c(Rank::Seven, Suit::Spades),
      c(Rank::Jack, Suit::Spades),  c(Rank::Queen, Suit::Spades),   c(Rank::King, Suit::Spades),
      c(Rank::Two, Suit::Diamonds), c(Rank::Nine, Suit::Diamonds)};
}

}  // namespace

TEST(GinDeal, TenEachTheUpcardOnOfferToTheOpener) {
  NoShuffleDealer dealer;
  auto deal = dealGin("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 1);
  ASSERT_TRUE(deal.ok()) << deal.status();
  EXPECT_EQ(deal->getPlayer(0).hand.size(), 10u);
  EXPECT_EQ(deal->getPlayer(1).hand.size(), 10u);
  EXPECT_EQ(deal->getDiscard().size(), 1u);
  EXPECT_EQ(deal->getStock().size(), 31u);
  EXPECT_EQ(deal->getWhoseTurn(), 1);
  EXPECT_EQ(deal->getStage(), GinStage::Upcard);
  EXPECT_FALSE(deal->canDrawStock());
  EXPECT_TRUE(deal->canDrawDiscard());
}

TEST(GinDeal, TwoSeatsOnly) {
  NoShuffleDealer dealer;
  EXPECT_FALSE(dealGin("g", {"a", "b", "c"}, dealer.DealNewUnshuffledDeck()).ok());
  EXPECT_FALSE(dealGin("g", {"a"}, dealer.DealNewUnshuffledDeck()).ok());
  EXPECT_FALSE(dealGin("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 2).ok());
}

TEST(GinUpcard, TheOpenerMayTakeIt) {
  NoShuffleDealer dealer;
  auto deal = dealGin("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 1);
  const Card up = deal->getDiscard().back();
  EXPECT_EQ(deal->drawStock(1).status().code(), absl::StatusCode::kFailedPrecondition);
  auto took = deal->drawDiscard(1);
  ASSERT_TRUE(took.ok()) << took.status();
  EXPECT_EQ(took->getStage(), GinStage::Play);
  EXPECT_EQ(took->getPlayer(1).hand.back(), up);
  EXPECT_EQ(took->getTakenDiscard(), up);
  EXPECT_TRUE(took->getDiscard().empty());
}

TEST(GinUpcard, PassedByBothTheOpenerDrawsFromTheStock) {
  NoShuffleDealer dealer;
  auto deal = dealGin("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 1);
  EXPECT_EQ(deal->pass(0).status().code(), absl::StatusCode::kFailedPrecondition);
  auto one = deal->pass(1);
  ASSERT_TRUE(one.ok());
  EXPECT_EQ(one->getWhoseTurn(), 0);
  EXPECT_EQ(one->getStage(), GinStage::Upcard);
  auto two = one->pass(0);
  ASSERT_TRUE(two.ok());
  EXPECT_EQ(two->getWhoseTurn(), 1);
  EXPECT_EQ(two->getStage(), GinStage::StockOnly);
  EXPECT_FALSE(two->canDrawDiscard());
  EXPECT_EQ(two->drawDiscard(1).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(two->pass(1).status().code(), absl::StatusCode::kFailedPrecondition);
  auto drew = two->drawStock(1);
  ASSERT_TRUE(drew.ok()) << drew.status();
  EXPECT_EQ(drew->getStage(), GinStage::Play);
  EXPECT_EQ(drew->getLastMove()->kind, GinMoveKind::DrawStock);
}

TEST(GinUpcard, TheDealerMayTakeItOnceTheOpenerPasses) {
  NoShuffleDealer dealer;
  auto one = dealGin("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 1)->pass(1);
  auto took = one->drawDiscard(0);
  ASSERT_TRUE(took.ok()) << took.status();
  EXPECT_EQ(took->getWhoseTurn(), 0);
  EXPECT_EQ(took->getStage(), GinStage::Play);
}

TEST(GinTurn, ADrawThenADiscardHandsItOn) {
  auto state = playing(knocker(), {c(Rank::Ace, Suit::Clubs)}, someStock(10),
                       {c(Rank::Six, Suit::Clubs)}, 0, GinStage::Play);
  auto threw = state.discard(0, c(Rank::Nine, Suit::Diamonds));
  ASSERT_TRUE(threw.ok()) << threw.status();
  EXPECT_EQ(threw->getWhoseTurn(), 1);
  EXPECT_EQ(threw->getStage(), GinStage::Draw);
  EXPECT_EQ(threw->getDiscard().back(), c(Rank::Nine, Suit::Diamonds));
  EXPECT_TRUE(threw->canDrawStock());
  EXPECT_TRUE(threw->canDrawDiscard());
  EXPECT_FALSE(threw->getTakenDiscard().has_value());
  EXPECT_EQ(threw->getLastMove()->kind, GinMoveKind::Discard);
}

TEST(GinTurn, RefusesOutOfTurnOutOfOrderAndCardsNotHeld) {
  auto state = playing(knocker(), {c(Rank::Ace, Suit::Clubs)}, someStock(10),
                       {c(Rank::Six, Suit::Clubs)}, 0, GinStage::Draw);
  EXPECT_EQ(state.drawStock(1).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(state.discard(0, c(Rank::Two, Suit::Diamonds)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(state.pass(0).status().code(), absl::StatusCode::kFailedPrecondition);
  auto drew = state.drawStock(0);
  ASSERT_TRUE(drew.ok());
  EXPECT_EQ(drew->discard(0, c(Rank::Ace, Suit::Clubs)).status().code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(drew->knock(0, c(Rank::Ace, Suit::Clubs)).status().code(), absl::StatusCode::kNotFound);
}

TEST(GinTurn, TheCardTakenCannotGoStraightBack) {
  const Card six = c(Rank::Six, Suit::Clubs);
  auto state =
      playing(knocker(), {c(Rank::Ace, Suit::Clubs)}, someStock(10), {six}, 0, GinStage::Draw);
  auto took = state.drawDiscard(0);
  ASSERT_TRUE(took.ok());
  EXPECT_EQ(took->discard(0, six).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(took->knock(0, six).status().code(), absl::StatusCode::kFailedPrecondition);
}

// Alice keeps 2♦ after throwing 9♦: two of deadwood. Bob lays 6♥ and 7♥
// off onto her hearts and A♠ onto her spades, and melds his clubs;
// K♦ K♣ Q♦ 5♠ are left, 35.
TEST(GinKnock, TheKnockerScoresTheDifferenceAfterLayOffs) {
  const vector<Card> bob = {c(Rank::Six, Suit::Hearts), c(Rank::Seven, Suit::Hearts),
                            c(Rank::Two, Suit::Clubs),  c(Rank::Three, Suit::Clubs),
                            c(Rank::Four, Suit::Clubs), c(Rank::King, Suit::Diamonds),
                            c(Rank::King, Suit::Clubs), c(Rank::Queen, Suit::Diamonds),
                            c(Rank::Ace, Suit::Spades), c(Rank::Five, Suit::Spades)};
  auto state =
      playing(knocker(), bob, someStock(10), {c(Rank::Six, Suit::Clubs)}, 0, GinStage::Play);
  auto knocked = state.knock(0, c(Rank::Nine, Suit::Diamonds));
  ASSERT_TRUE(knocked.ok()) << knocked.status();
  EXPECT_EQ(knocked->getPhase(), Phase::Over);
  EXPECT_EQ(knocked->getWhoseTurn(), GinState::kNoTurn);
  const GinResult& result = *knocked->getResult();
  EXPECT_EQ(result.ending, GinEnding::Knock);
  EXPECT_EQ(result.knocker, 0);
  EXPECT_EQ(result.winner, 0);
  EXPECT_EQ(result.hands.at(0).deadwoodPoints, 2);
  EXPECT_EQ(result.hands.at(1).deadwoodPoints, 35);
  EXPECT_EQ(result.laidOff.size(), 3u);
  EXPECT_EQ(result.points, 33);
  EXPECT_EQ(knocked->winner(), "alice");
  EXPECT_EQ(knocked->winnerPoints(), 33);
  EXPECT_EQ(knocked->deadwood(1), 35);
  EXPECT_EQ(knocked->getDiscard().back(), c(Rank::Nine, Suit::Diamonds));
  EXPECT_EQ(knocked->getLastMove()->kind, GinMoveKind::Knock);
}

TEST(GinKnock, MoreThanTenDeadwoodCannotKnock) {
  vector<Card> hand = knocker();
  hand[9] = c(Rank::King, Suit::Diamonds);  // K♦ for 2♦: 10 plus the 9♦
  auto state = playing(hand, {c(Rank::Ace, Suit::Clubs)}, someStock(10), {}, 0, GinStage::Play);
  // Throwing K♦ leaves the 9♦: nine, and a knock.
  EXPECT_TRUE(state.knock(0, c(Rank::King, Suit::Diamonds)).ok());
  // Throwing a seven breaks the set: far over ten.
  const absl::Status over = state.knock(0, c(Rank::Seven, Suit::Clubs)).status();
  EXPECT_EQ(over.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(over.message(), "more than 10 deadwood: no knock");
}

TEST(GinKnock, ExactlyTenMayKnock) {
  vector<Card> hand = knocker();
  hand[9] = c(Rank::Ten, Suit::Diamonds);  // 10♦ for 2♦
  auto state = playing(hand, {c(Rank::Ace, Suit::Clubs)}, someStock(10), {}, 0, GinStage::Play);
  // Throwing the 9♦ leaves the 10♦: ten, the most a knock may carry.
  EXPECT_TRUE(state.knock(0, c(Rank::Nine, Suit::Diamonds)).ok());
}

// No deadwood: gin. Bob may not lay off, and alice scores 25 and all he
// holds.
TEST(GinKnock, GinScoresTheBonusAndBlocksLayOffs) {
  vector<Card> alice = knocker();
  alice[9] = c(Rank::Six, Suit::Hearts);  // 3♥-6♥ for 2♦
  const vector<Card> bob = {c(Rank::Seven, Suit::Hearts),   c(Rank::Two, Suit::Clubs),
                            c(Rank::Three, Suit::Clubs),    c(Rank::Four, Suit::Clubs),
                            c(Rank::King, Suit::Diamonds),  c(Rank::King, Suit::Clubs),
                            c(Rank::Queen, Suit::Diamonds), c(Rank::Ace, Suit::Spades),
                            c(Rank::Five, Suit::Spades),    c(Rank::Eight, Suit::Hearts)};
  auto knocked = playing(alice, bob, someStock(10), {}, 0, GinStage::Play)
                     .knock(0, c(Rank::Nine, Suit::Diamonds));
  ASSERT_TRUE(knocked.ok()) << knocked.status();
  const GinResult& result = *knocked->getResult();
  EXPECT_EQ(result.ending, GinEnding::Gin);
  EXPECT_TRUE(result.laidOff.empty());
  EXPECT_EQ(result.hands.at(1).deadwoodPoints, 51);
  EXPECT_EQ(result.points, 76);
  EXPECT_EQ(result.winner, 0);
}

// Bob's clubs, hearts and fives meld, all but A♥: one point. Against a
// knock on as much or more, he undercuts: 25 and the difference.
TEST(GinKnock, ADefenderWithNoMoreDeadwoodUndercuts) {
  const vector<Card> alice = {
      c(Rank::Three, Suit::Hearts), c(Rank::Four, Suit::Hearts),    c(Rank::Five, Suit::Hearts),
      c(Rank::Seven, Suit::Clubs),  c(Rank::Seven, Suit::Diamonds), c(Rank::Seven, Suit::Spades),
      c(Rank::Jack, Suit::Spades),  c(Rank::Queen, Suit::Spades),   c(Rank::King, Suit::Spades),
      c(Rank::Ace, Suit::Diamonds), c(Rank::Eight, Suit::Diamonds)};
  const vector<Card> bob = {c(Rank::Two, Suit::Clubs),     c(Rank::Three, Suit::Clubs),
                            c(Rank::Four, Suit::Clubs),    c(Rank::Nine, Suit::Hearts),
                            c(Rank::Ten, Suit::Hearts),    c(Rank::Jack, Suit::Hearts),
                            c(Rank::Five, Suit::Diamonds), c(Rank::Five, Suit::Spades),
                            c(Rank::Five, Suit::Clubs),    c(Rank::Ace, Suit::Hearts)};
  // Alice throws 8♦ and knocks on A♦, one point: bob's A♥ ties it.
  auto tied = playing(alice, bob, someStock(10), {}, 0, GinStage::Play)
                  .knock(0, c(Rank::Eight, Suit::Diamonds));
  ASSERT_TRUE(tied.ok()) << tied.status();
  EXPECT_EQ(tied->getResult()->ending, GinEnding::Undercut);
  EXPECT_EQ(tied->getResult()->winner, 1);
  EXPECT_EQ(tied->getResult()->points, 25);
  EXPECT_EQ(tied->winner(), "bob");
  // Throwing A♦ instead leaves 8♦: eight against one.
  auto under = playing(alice, bob, someStock(10), {}, 0, GinStage::Play)
                   .knock(0, c(Rank::Ace, Suit::Diamonds));
  ASSERT_TRUE(under.ok()) << under.status();
  EXPECT_EQ(under->getResult()->ending, GinEnding::Undercut);
  EXPECT_EQ(under->getResult()->points, 25 + 7);
}

TEST(GinStock, ADiscardLeavingTwoInTheStockDrawsTheDeal) {
  auto state = playing(knocker(), {c(Rank::King, Suit::Clubs)}, someStock(3),
                       {c(Rank::Six, Suit::Clubs)}, 0, GinStage::Draw);
  auto drew = state.drawStock(0);
  ASSERT_TRUE(drew.ok());
  ASSERT_EQ(drew->getStock().size(), 2u);
  // A knock still ends it by knocking.
  EXPECT_EQ(drew->knock(0, c(Rank::Nine, Suit::Diamonds))->getResult()->ending, GinEnding::Knock);
  auto threw = drew->discard(0, c(Rank::Nine, Suit::Diamonds));
  ASSERT_TRUE(threw.ok());
  EXPECT_EQ(threw->getPhase(), Phase::Over);
  EXPECT_EQ(threw->getResult()->ending, GinEnding::Draw);
  EXPECT_FALSE(threw->winner().has_value());
  EXPECT_EQ(threw->winnerPoints(), 0);
}

TEST(GinLeave, ALeaveAbandonsTheDeal) {
  auto state =
      playing(knocker(), {c(Rank::Ace, Suit::Clubs)}, someStock(10), {}, 1, GinStage::Draw);
  auto left = state.removePlayer(0);
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(left->getPhase(), Phase::Abandoned);
  EXPECT_EQ(left->getPlayers().size(), 1u);
  EXPECT_EQ(left->getPlayer(0).id, "bob");
  EXPECT_FALSE(left->winner().has_value());
  EXPECT_EQ(left->getWhoseTurn(), GinState::kNoTurn);
  EXPECT_FALSE(left->removePlayer(0).ok());
}
