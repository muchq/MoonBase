#include "domains/games/libs/cards/rummy/game_state.h"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"

using namespace cards;
using namespace rummy;
using std::deque;
using std::string;
using std::vector;

namespace {

Card c(Rank rank, Suit suit = Suit::Clubs) { return Card{suit, rank}; }

/// A game in play: seats as given, piles' backs on top, seat `turn` to
/// move at `stage`.
GameState playing(vector<Player> players, deque<Card> stock, vector<Card> discard, int turn = 0,
                  Stage stage = Stage::Draw, vector<Meld> melds = {},
                  std::optional<Card> taken = std::nullopt) {
  return GameState{std::move(stock),
                   std::move(discard),
                   std::move(players),
                   std::move(melds),
                   turn,
                   stage,
                   Phase::Playing,
                   std::move(taken),
                   "game",
                   "v0"};
}

// Two seats, alice to play (she has drawn): a run and a set to go down,
// and odd cards to discard.
GameState alicePlaying() {
  return playing(
      {{"alice",
        {c(Rank::Four), c(Rank::Five), c(Rank::Six), c(Rank::Nine, Suit::Hearts),
         c(Rank::Nine, Suit::Spades), c(Rank::Nine, Suit::Diamonds), c(Rank::King, Suit::Hearts)}},
       {"bob", {c(Rank::Two, Suit::Hearts), c(Rank::Jack, Suit::Spades)}}},
      {c(Rank::Three, Suit::Spades)}, {c(Rank::Queen, Suit::Diamonds)}, 0, Stage::Play);
}

}  // namespace

// --- The deal ---

TEST(Deal, TwoSeatsGetSevenOneCardIsTurnedUpAndTheRestIsStock) {
  NoShuffleDealer dealer;
  auto game = dealRummyGame("g1", {"a", "b"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(game.ok()) << game.status();
  EXPECT_EQ(game->getPhase(), Phase::Playing);
  EXPECT_EQ(game->getStage(), Stage::Draw);
  EXPECT_EQ(game->getWhoseTurn(), 0);
  EXPECT_EQ(game->getGameId(), "g1");
  ASSERT_EQ(game->getPlayers().size(), 2u);
  EXPECT_EQ(game->getPlayer(0).hand.size(), 7u);
  EXPECT_EQ(game->getPlayer(1).hand.size(), 7u);
  EXPECT_EQ(game->getDiscard().size(), 1u);
  EXPECT_EQ(game->getStock().size(), 52u - 14u - 1u);
  EXPECT_TRUE(game->getMelds().empty());
  EXPECT_FALSE(game->getLastMove().has_value());
  EXPECT_FALSE(game->getTakenDiscard().has_value());
}

// Ten-card rummy (#1609) is the same game dealt ten a seat.
TEST(Deal, AHandSizeCanBeAskedFor) {
  NoShuffleDealer dealer;
  auto game = dealRummyGame("g1", {"a", "b", "c"}, dealer.DealNewUnshuffledDeck(), 0, 10);
  ASSERT_TRUE(game.ok()) << game.status();
  for (const Player& p : game->getPlayers()) EXPECT_EQ(p.hand.size(), 10u);
  EXPECT_EQ(game->getStock().size(), 52u - 30u - 1u);
  EXPECT_FALSE(dealRummyGame("g1", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 0, 0).ok());
}

TEST(Deal, DealsOneCardASeatAroundTheTableFromTheBack) {
  // The unshuffled deck's back is A♠ A♥ A♦ A♣ K♠ ...: seat 0 takes every
  // other card from it, and the card after the hands is turned up.
  NoShuffleDealer dealer;
  auto game = dealRummyGame("g1", {"a", "b"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(game.ok());
  const vector<Card>& a = game->getPlayer(0).hand;
  const vector<Card>& b = game->getPlayer(1).hand;
  EXPECT_EQ(a.at(0), c(Rank::Ace, Suit::Spades));
  EXPECT_EQ(b.at(0), c(Rank::Ace, Suit::Hearts));
  EXPECT_EQ(a.at(1), c(Rank::Ace, Suit::Diamonds));
  EXPECT_EQ(b.at(1), c(Rank::Ace, Suit::Clubs));
  EXPECT_EQ(a.at(6), c(Rank::Jack, Suit::Spades));
  EXPECT_EQ(b.at(6), c(Rank::Jack, Suit::Hearts));
  EXPECT_EQ(game->getDiscard().back(), c(Rank::Jack, Suit::Diamonds));
  EXPECT_EQ(game->getStock().back(), c(Rank::Jack, Suit::Clubs));
}

TEST(Deal, ThreeOrFourSeatsGetSeven) {
  for (int seats : {3, 4}) {
    NoShuffleDealer dealer;
    vector<string> ids;
    for (int i = 0; i < seats; i++) ids.push_back(string(1, static_cast<char>('a' + i)));
    auto game = dealRummyGame("g1", ids, dealer.DealNewUnshuffledDeck());
    ASSERT_TRUE(game.ok());
    for (const Player& p : game->getPlayers()) EXPECT_EQ(p.hand.size(), 7u) << seats;
    EXPECT_EQ(game->getStock().size(), static_cast<size_t>(52 - 7 * seats - 1));
  }
}

TEST(Deal, TakesTwoToFourSeats) {
  NoShuffleDealer dealer;
  EXPECT_FALSE(dealRummyGame("g", {"a"}, dealer.DealNewUnshuffledDeck()).ok());
  EXPECT_FALSE(dealRummyGame("g", {"a", "b", "c", "d", "e"}, dealer.DealNewUnshuffledDeck()).ok());
}

TEST(Deal, RefusesADeckTooSmallForTheHandsAndTheTurnedCard) {
  deque<Card> deck;
  for (int i = 0; i < 14; i++) deck.emplace_back(i);
  EXPECT_FALSE(dealRummyGame("g", {"a", "b"}, deck).ok());
  deck.emplace_back(14);
  EXPECT_TRUE(dealRummyGame("g", {"a", "b"}, deck).ok());
}

// --- The draw ---

TEST(Draw, FromTheStockTakesItsTopAndOpensThePlay) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}},
                      {c(Rank::Four), c(Rank::Five)}, {c(Rank::Six)});
  auto next = game.drawStock(0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getPlayer(0).hand, (vector<Card>{c(Rank::Two), c(Rank::Five)}));
  EXPECT_EQ(next->getStock(), (deque<Card>{c(Rank::Four)}));
  EXPECT_EQ(next->getStage(), Stage::Play);
  EXPECT_EQ(next->getWhoseTurn(), 0);
  EXPECT_FALSE(next->getTakenDiscard().has_value());
  // Nobody else sees the card: the move names none.
  ASSERT_TRUE(next->getLastMove().has_value());
  EXPECT_EQ(next->getLastMove()->kind, MoveKind::DrawStock);
  EXPECT_EQ(next->getLastMove()->playerId, "alice");
  EXPECT_TRUE(next->getLastMove()->cards.empty());
}

TEST(Draw, FromTheDiscardTakesItsTopAndRemembersIt) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {c(Rank::Four)},
                      {c(Rank::Five), c(Rank::Six)});
  auto next = game.drawDiscard(0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getPlayer(0).hand, (vector<Card>{c(Rank::Two), c(Rank::Six)}));
  EXPECT_EQ(next->getDiscard(), (vector<Card>{c(Rank::Five)}));
  EXPECT_EQ(next->getTakenDiscard(), c(Rank::Six));
  EXPECT_EQ(next->getStage(), Stage::Play);
  EXPECT_EQ(next->getLastMove()->kind, MoveKind::DrawDiscard);
  EXPECT_EQ(next->getLastMove()->cards, vector<Card>{c(Rank::Six)});
}

TEST(Draw, OnlyOnePerTurn) {
  auto drawn = alicePlaying();
  EXPECT_EQ(drawn.drawStock(0).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(drawn.drawDiscard(0).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(Draw, OnlyOnYourTurn) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {c(Rank::Four)},
                      {c(Rank::Five)});
  EXPECT_EQ(game.drawStock(1).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(game.drawDiscard(1).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(game.drawStock(2).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Draw, AnEmptyStockIsTheDiscardPileTurnedOverUnderItsTop) {
  // Discard bottom to top: 4 5 6 7. Turned over, 4 is the stock's top,
  // and 7 stays face up.
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {},
                      {c(Rank::Four), c(Rank::Five), c(Rank::Six), c(Rank::Seven)});
  EXPECT_TRUE(game.canDrawStock());
  auto next = game.drawStock(0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getPlayer(0).hand, (vector<Card>{c(Rank::Two), c(Rank::Four)}));
  EXPECT_EQ(next->getStock(), (deque<Card>{c(Rank::Six), c(Rank::Five)}));
  EXPECT_EQ(next->getDiscard(), vector<Card>{c(Rank::Seven)});
}

TEST(Draw, WithNothingUnderTheDiscardTopThereIsNoStock) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {}, {c(Rank::Seven)});
  EXPECT_FALSE(game.canDrawStock());
  EXPECT_EQ(game.drawStock(0).status().code(), absl::StatusCode::kFailedPrecondition);
  // The discard is still there to take.
  EXPECT_TRUE(game.drawDiscard(0).ok());
}

TEST(Draw, AnEmptyDiscardPileHasNothingToTake) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {c(Rank::Four)}, {});
  EXPECT_EQ(game.drawDiscard(0).status().code(), absl::StatusCode::kFailedPrecondition);
}

// --- Melds ---

TEST(Meld, NotBeforeTheDraw) {
  auto game = playing({{"alice", {c(Rank::Four), c(Rank::Five), c(Rank::Six), c(Rank::Ten)}},
                       {"bob", {c(Rank::Three)}}},
                      {c(Rank::Two)}, {c(Rank::Nine)});
  EXPECT_EQ(game.meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Six)}).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(Meld, PutsTheCardsOnTheTableInOrderAndTheTurnStays) {
  auto next = alicePlaying().meld(0, {c(Rank::Six), c(Rank::Four), c(Rank::Five)});
  ASSERT_TRUE(next.ok()) << next.status();
  ASSERT_EQ(next->getMelds().size(), 1u);
  EXPECT_EQ(next->getMelds()[0].owner, "alice");
  EXPECT_EQ(next->getMelds()[0].cards, (vector<Card>{c(Rank::Four), c(Rank::Five), c(Rank::Six)}));
  EXPECT_EQ(next->getPlayer(0).hand,
            (vector<Card>{c(Rank::Nine, Suit::Hearts), c(Rank::Nine, Suit::Spades),
                          c(Rank::Nine, Suit::Diamonds), c(Rank::King, Suit::Hearts)}));
  EXPECT_EQ(next->getWhoseTurn(), 0);
  EXPECT_EQ(next->getStage(), Stage::Play);
  EXPECT_EQ(next->getLastMove()->kind, MoveKind::Meld);
  EXPECT_EQ(next->getLastMove()->meld, 0);
  EXPECT_EQ(next->getLastMove()->cards, next->getMelds()[0].cards);
}

TEST(Meld, SeveralInOneTurn) {
  auto one = alicePlaying().meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Six)});
  ASSERT_TRUE(one.ok());
  auto two = one->meld(
      0, {c(Rank::Nine, Suit::Hearts), c(Rank::Nine, Suit::Spades), c(Rank::Nine, Suit::Diamonds)});
  ASSERT_TRUE(two.ok()) << two.status();
  EXPECT_EQ(two->getMelds().size(), 2u);
  EXPECT_EQ(two->getLastMove()->meld, 1);
  EXPECT_EQ(two->getPlayer(0).hand, vector<Card>{c(Rank::King, Suit::Hearts)});
}

TEST(Meld, CardsThatMakeNoMeldAreRefusedAndNothingMoves) {
  auto game = alicePlaying();
  auto refused = game.meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::King, Suit::Hearts)});
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Meld, ACardTheHandDoesNotHoldIsNotFound) {
  auto refused = alicePlaying().meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Three)});
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kNotFound);
}

TEST(Meld, OnlyOnYourTurn) {
  auto game = alicePlaying();
  EXPECT_EQ(game.meld(1, {c(Rank::Two, Suit::Hearts)}).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

// --- Lay-offs ---

TEST(LayOff, GrowsAMeldAnyoneLaid) {
  auto game = playing({{"alice", {c(Rank::Seven), c(Rank::King)}}, {"bob", {c(Rank::Three)}}}, {},
                      {c(Rank::Two)}, 0, Stage::Play,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  auto next = game.layOff(0, c(Rank::Seven), 0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getMelds()[0].owner, "bob");
  EXPECT_EQ(next->getMelds()[0].cards,
            (vector<Card>{c(Rank::Four), c(Rank::Five), c(Rank::Six), c(Rank::Seven)}));
  EXPECT_EQ(next->getPlayer(0).hand, vector<Card>{c(Rank::King)});
  EXPECT_EQ(next->getLastMove()->kind, MoveKind::LayOff);
  EXPECT_EQ(next->getLastMove()->meld, 0);
  EXPECT_EQ(next->getLastMove()->cards, vector<Card>{c(Rank::Seven)});
}

TEST(LayOff, AtTheLowEndOfARunOrOntoASet) {
  auto game = playing(
      {{"alice", {c(Rank::Three), c(Rank::Nine, Suit::Spades), c(Rank::King)}},
       {"bob", {c(Rank::Three, Suit::Hearts)}}},
      {}, {c(Rank::Two)}, 0, Stage::Play,
      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}},
       {"bob",
        {c(Rank::Nine, Suit::Clubs), c(Rank::Nine, Suit::Diamonds), c(Rank::Nine, Suit::Hearts)}}});
  auto low = game.layOff(0, c(Rank::Three), 0);
  ASSERT_TRUE(low.ok()) << low.status();
  EXPECT_EQ(low->getMelds()[0].cards.front(), c(Rank::Three));
  auto set = low->layOff(0, c(Rank::Nine, Suit::Spades), 1);
  ASSERT_TRUE(set.ok()) << set.status();
  EXPECT_EQ(set->getMelds()[1].cards.size(), 4u);
}

TEST(LayOff, ACardThatBreaksTheMeldIsRefused) {
  auto game = playing({{"alice", {c(Rank::Eight), c(Rank::King)}}, {"bob", {c(Rank::Three)}}}, {},
                      {c(Rank::Two)}, 0, Stage::Play,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  EXPECT_EQ(game.layOff(0, c(Rank::Eight), 0).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayOff, NamesAMeldOnTheTable) {
  auto game = playing({{"alice", {c(Rank::Seven), c(Rank::King)}}, {"bob", {c(Rank::Three)}}}, {},
                      {c(Rank::Two)}, 0, Stage::Play,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  EXPECT_EQ(game.layOff(0, c(Rank::Seven), 1).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(game.layOff(0, c(Rank::Seven), -1).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayOff, ACardTheHandDoesNotHoldIsNotFound) {
  auto game = playing({{"alice", {c(Rank::Seven), c(Rank::King)}}, {"bob", {c(Rank::Three)}}}, {},
                      {c(Rank::Two)}, 0, Stage::Play,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  EXPECT_EQ(game.layOff(0, c(Rank::Three), 0).status().code(), absl::StatusCode::kNotFound);
}

TEST(LayOff, NotBeforeTheDraw) {
  auto game = playing({{"alice", {c(Rank::Seven), c(Rank::King)}}, {"bob", {c(Rank::Three)}}},
                      {c(Rank::Two)}, {c(Rank::Two, Suit::Hearts)}, 0, Stage::Draw,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  EXPECT_EQ(game.layOff(0, c(Rank::Seven), 0).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

// --- The ace: low under the two or high over the king, never both ---

TEST(Ace, MeldsAtEitherEndOfARunButNeverAroundTheCorner) {
  auto game = playing(
      {{"alice",
        {c(Rank::Queen), c(Rank::King), c(Rank::Ace), c(Rank::Ace, Suit::Hearts),
         c(Rank::Two, Suit::Hearts), c(Rank::Three, Suit::Hearts), c(Rank::King, Suit::Spades),
         c(Rank::Ace, Suit::Spades), c(Rank::Two, Suit::Spades), c(Rank::Nine, Suit::Diamonds)}},
       {"bob", {c(Rank::Three, Suit::Diamonds)}}},
      {}, {c(Rank::Two)}, 0, Stage::Play);
  auto high = game.meld(0, {c(Rank::Queen), c(Rank::King), c(Rank::Ace)});
  ASSERT_TRUE(high.ok()) << high.status();
  auto low = high->meld(
      0, {c(Rank::Ace, Suit::Hearts), c(Rank::Two, Suit::Hearts), c(Rank::Three, Suit::Hearts)});
  ASSERT_TRUE(low.ok()) << low.status();
  EXPECT_EQ(low->meld(0, {c(Rank::King, Suit::Spades), c(Rank::Ace, Suit::Spades),
                          c(Rank::Two, Suit::Spades)})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(Ace, LaysOffAtEitherEndButNeverAroundTheCorner) {
  auto game = playing(
      {{"alice",
        {c(Rank::Ace), c(Rank::Ace, Suit::Hearts), c(Rank::Two, Suit::Spades),
         c(Rank::King, Suit::Diamonds), c(Rank::Nine, Suit::Diamonds)}},
       {"bob", {c(Rank::Three, Suit::Diamonds)}}},
      {}, {c(Rank::Two)}, 0, Stage::Play,
      {{"bob", {c(Rank::Jack), c(Rank::Queen), c(Rank::King)}},
       {"bob",
        {c(Rank::Two, Suit::Hearts), c(Rank::Three, Suit::Hearts), c(Rank::Four, Suit::Hearts)}},
       {"bob",
        {c(Rank::Queen, Suit::Spades), c(Rank::King, Suit::Spades), c(Rank::Ace, Suit::Spades)}},
       {"bob",
        {c(Rank::Ace, Suit::Diamonds), c(Rank::Two, Suit::Diamonds),
         c(Rank::Three, Suit::Diamonds)}}});
  auto high = game.layOff(0, c(Rank::Ace), 0);
  ASSERT_TRUE(high.ok()) << high.status();
  EXPECT_EQ(high->getMelds()[0].cards.back(), c(Rank::Ace));
  auto low = high->layOff(0, c(Rank::Ace, Suit::Hearts), 1);
  ASSERT_TRUE(low.ok()) << low.status();
  EXPECT_EQ(low->getMelds()[1].cards.front(), c(Rank::Ace, Suit::Hearts));
  // A two after the high ace, or a king under the low one, wraps.
  EXPECT_EQ(low->layOff(0, c(Rank::Two, Suit::Spades), 2).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(low->layOff(0, c(Rank::King, Suit::Diamonds), 3).status().code(),
            absl::StatusCode::kInvalidArgument);
}

// --- The discard ---

TEST(Discard, EndsTheTurnAtTheNextSeatsDraw) {
  auto next = alicePlaying().discard(0, c(Rank::King, Suit::Hearts));
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getDiscard().back(), c(Rank::King, Suit::Hearts));
  EXPECT_EQ(next->getWhoseTurn(), 1);
  EXPECT_EQ(next->getStage(), Stage::Draw);
  EXPECT_EQ(next->getPlayer(0).hand.size(), 6u);
  EXPECT_EQ(next->getLastMove()->kind, MoveKind::Discard);
  EXPECT_EQ(next->getLastMove()->cards, vector<Card>{c(Rank::King, Suit::Hearts)});
}

TEST(Discard, TheTurnWrapsAroundTheTable) {
  auto game = playing(
      {{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}, {"c", {c(Rank::Four), c(Rank::Five)}}},
      {c(Rank::Six)}, {c(Rank::Seven)}, 2, Stage::Play);
  auto next = game.discard(2, c(Rank::Four));
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(next->getWhoseTurn(), 0);
}

TEST(Discard, NotBeforeTheDraw) {
  auto game = playing({{"alice", {c(Rank::Two), c(Rank::Three)}}, {"bob", {c(Rank::Three)}}},
                      {c(Rank::Four)}, {c(Rank::Five)});
  EXPECT_EQ(game.discard(0, c(Rank::Two)).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(Discard, ACardTheHandDoesNotHoldIsNotFound) {
  EXPECT_EQ(alicePlaying().discard(0, c(Rank::Ace)).status().code(), absl::StatusCode::kNotFound);
}

TEST(Discard, TheTakenCardStaysBarredAfterAMeld) {
  // The bar is the turn's, not the draw's: melding in between lifts it
  // only if the taken card is then all the hand holds.
  auto game = playing({{"alice", {c(Rank::Four), c(Rank::Five), c(Rank::Six), c(Rank::Two)}},
                       {"bob", {c(Rank::Three)}}},
                      {c(Rank::Ace)}, {c(Rank::King)});
  auto drew = game.drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  auto melded = drew->meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Six)});
  ASSERT_TRUE(melded.ok());
  EXPECT_EQ(melded->getTakenDiscard(), c(Rank::King));
  EXPECT_EQ(melded->discard(0, c(Rank::King)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(melded->discard(0, c(Rank::Two)).ok());
}

TEST(Discard, TheCardTakenFromTheDiscardCannotGoStraightBack) {
  auto game = playing({{"alice", {c(Rank::Two), c(Rank::Three)}}, {"bob", {c(Rank::Three)}}},
                      {c(Rank::Four)}, {c(Rank::Five)});
  auto drew = game.drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  EXPECT_EQ(drew->discard(0, c(Rank::Five)).status().code(), absl::StatusCode::kFailedPrecondition);
  auto other = drew->discard(0, c(Rank::Two));
  ASSERT_TRUE(other.ok()) << other.status();
  // The next turn is its own: nothing is remembered past the discard.
  EXPECT_FALSE(other->getTakenDiscard().has_value());
}

TEST(Discard, TheTakenCardMayGoBackWhenItIsTheLastInTheHand) {
  // Everything else went down: holding nothing else, the seat may throw
  // it and go out rather than be stuck.
  auto game =
      playing({{"alice", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}, {"bob", {c(Rank::Three)}}},
              {c(Rank::Two)}, {c(Rank::King)});
  auto drew = game.drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  auto melded = drew->meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Six)});
  ASSERT_TRUE(melded.ok());
  auto out = melded->discard(0, c(Rank::King));
  ASSERT_TRUE(out.ok()) << out.status();
  EXPECT_EQ(out->winner(), "alice");
}

TEST(Discard, AStockDrawRemembersNothing) {
  auto game = playing({{"alice", {c(Rank::Two)}}, {"bob", {c(Rank::Three)}}}, {c(Rank::Five)},
                      {c(Rank::Five, Suit::Hearts)});
  auto drew = game.drawStock(0);
  ASSERT_TRUE(drew.ok());
  EXPECT_TRUE(drew->discard(0, c(Rank::Five)).ok());
}

// --- Going out ---

TEST(GoingOut, AnEmptyHandAfterTheDiscardWinsAndScoresTheRestsHands) {
  auto game = playing({{"alice", {c(Rank::King)}},
                       {"bob", {c(Rank::Ace), c(Rank::Seven), c(Rank::Queen)}},
                       {"carol", {c(Rank::Two, Suit::Hearts)}}},
                      {c(Rank::Three)}, {c(Rank::Four)}, 0, Stage::Play);
  auto out = game.discard(0, c(Rank::King));
  ASSERT_TRUE(out.ok()) << out.status();
  EXPECT_EQ(out->getPhase(), Phase::Over);
  EXPECT_TRUE(out->isOver());
  EXPECT_EQ(out->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_EQ(out->winner(), "alice");
  EXPECT_EQ(out->deadwood(0), 0);
  EXPECT_EQ(out->deadwood(1), 1 + 7 + 10);
  EXPECT_EQ(out->deadwood(2), 2);
  EXPECT_EQ(out->winnerPoints(), 20);
}

TEST(GoingOut, ByMeldingEverythingNeedsNoDiscard) {
  auto game =
      playing({{"alice", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}, {"bob", {c(Rank::King)}}},
              {c(Rank::Two)}, {c(Rank::Nine)}, 0, Stage::Play);
  auto out = game.meld(0, {c(Rank::Four), c(Rank::Five), c(Rank::Six)});
  ASSERT_TRUE(out.ok()) << out.status();
  EXPECT_EQ(out->getPhase(), Phase::Over);
  EXPECT_EQ(out->winner(), "alice");
  EXPECT_EQ(out->winnerPoints(), 10);
}

TEST(GoingOut, ByLayingOffTheLastCard) {
  auto game = playing({{"alice", {c(Rank::Seven)}}, {"bob", {c(Rank::King)}}}, {c(Rank::Two)},
                      {c(Rank::Nine)}, 0, Stage::Play,
                      {{"bob", {c(Rank::Four), c(Rank::Five), c(Rank::Six)}}});
  auto out = game.layOff(0, c(Rank::Seven), 0);
  ASSERT_TRUE(out.ok()) << out.status();
  EXPECT_EQ(out->winner(), "alice");
}

TEST(GoingOut, NothingMovesOnceTheGameIsOver) {
  auto game = playing({{"alice", {c(Rank::King)}}, {"bob", {c(Rank::Ace), c(Rank::Two)}}},
                      {c(Rank::Three)}, {c(Rank::Four)}, 0, Stage::Play);
  auto out = game.discard(0, c(Rank::King));
  ASSERT_TRUE(out.ok());
  for (int seat : {0, 1}) {
    EXPECT_FALSE(out->drawStock(seat).ok());
    EXPECT_FALSE(out->drawDiscard(seat).ok());
    EXPECT_FALSE(out->discard(seat, c(Rank::Ace)).ok());
    EXPECT_FALSE(out->meld(seat, {c(Rank::Ace)}).ok());
  }
  EXPECT_FALSE(out->removePlayer(1).ok());
}

TEST(GoingOut, NoWinnerWhileThePlayGoesOn) {
  EXPECT_FALSE(alicePlaying().winner().has_value());
  EXPECT_EQ(alicePlaying().winnerPoints(), 0);
}

// --- Leaving ---

TEST(Leave, OffTurnTheSeatGoesAndTheTurnFollowsItsHolder) {
  auto game = playing({{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}, {"c", {c(Rank::Four)}}},
                      {c(Rank::Five)}, {c(Rank::Six)}, 2, Stage::Play);
  auto next = game.removePlayer(0);
  ASSERT_TRUE(next.ok()) << next.status();
  ASSERT_EQ(next->getPlayers().size(), 2u);
  EXPECT_EQ(next->getPlayer(next->getWhoseTurn()).id, "c");
  EXPECT_EQ(next->getStage(), Stage::Play);
  EXPECT_EQ(next->getPhase(), Phase::Playing);
}

TEST(Leave, OnTurnTheNextSeatDraws) {
  auto game = playing({{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}, {"c", {c(Rank::Four)}}},
                      {c(Rank::Five)}, {c(Rank::Six)}, 1, Stage::Play, {}, c(Rank::Seven));
  auto next = game.removePlayer(1);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getPlayer(next->getWhoseTurn()).id, "c");
  EXPECT_EQ(next->getStage(), Stage::Draw);
  EXPECT_FALSE(next->getTakenDiscard().has_value());
}

TEST(Leave, OnTurnInTheLastSeatWrapsToTheFirst) {
  auto game = playing({{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}, {"c", {c(Rank::Four)}}},
                      {c(Rank::Five)}, {c(Rank::Six)}, 2, Stage::Draw);
  auto next = game.removePlayer(2);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(next->getPlayer(next->getWhoseTurn()).id, "a");
}

TEST(Leave, TheLeaversMeldsStayOnTheTable) {
  auto game = playing({{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}, {"c", {c(Rank::Four)}}},
                      {c(Rank::Five)}, {c(Rank::Six)}, 0, Stage::Draw,
                      {{"b", {c(Rank::Nine), c(Rank::Ten), c(Rank::Jack)}}});
  auto next = game.removePlayer(1);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(next->getMelds(), game.getMelds());
}

TEST(Leave, BelowTwoSeatsTheGameIsAbandonedWithNoWinner) {
  auto game =
      playing({{"a", {c(Rank::Two)}}, {"b", {c(Rank::Three)}}}, {c(Rank::Five)}, {c(Rank::Six)});
  auto next = game.removePlayer(0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getPhase(), Phase::Abandoned);
  EXPECT_TRUE(next->isOver());
  EXPECT_FALSE(next->winner().has_value());
  EXPECT_EQ(next->winnerPoints(), 0);
  EXPECT_EQ(next->getWhoseTurn(), GameState::kNoTurn);
}

TEST(Leave, NamesASeat) {
  EXPECT_EQ(alicePlaying().removePlayer(5).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Queries, PlayerIndexByIdAndIdAndVersionAreTheRowsToSet) {
  auto game = alicePlaying();
  EXPECT_EQ(game.playerIndex("bob"), 1);
  EXPECT_EQ(game.playerIndex("zed"), -1);
  auto stamped = game.withIdAndVersion("G", "V");
  EXPECT_EQ(stamped.getGameId(), "G");
  EXPECT_EQ(stamped.getVersionId(), "V");
  EXPECT_EQ(stamped.getPlayers(), game.getPlayers());
}

// --- Taking down into the discard pile ---
//
// A draw may take the discard pile's top card, or every card from the top
// down to one named deeper in the pile. Taking more than one card binds the
// seat to play the deepest card taken — meld it or lay it off — before its
// turn can end, so a seat may only take down to a card it can play.

namespace {

// Alice to draw. The pile, bottom to top: 2♣ 5♥ 6♥ K♠. She holds 7♥ 9♦ J♣.
GameState aliceToDraw(vector<Meld> table = {}, vector<Card> hand = {}) {
  if (hand.empty()) {
    hand = {c(Rank::Seven, Suit::Hearts), c(Rank::Nine, Suit::Diamonds), c(Rank::Jack)};
  }
  return playing({{"alice", std::move(hand)}, {"bob", {c(Rank::Three, Suit::Spades)}}},
                 {c(Rank::Four, Suit::Spades)},
                 {c(Rank::Two), c(Rank::Five, Suit::Hearts), c(Rank::Six, Suit::Hearts),
                  c(Rank::King, Suit::Spades)},
                 0, Stage::Draw, std::move(table));
}

}  // namespace

TEST(TakeDown, NamingTheTopCardIsTheOrdinaryDraw) {
  auto took = aliceToDraw().drawDiscard(0, c(Rank::King, Suit::Spades));
  ASSERT_TRUE(took.ok()) << took.status();
  EXPECT_EQ(took->getTakenDiscard(), c(Rank::King, Suit::Spades));
  EXPECT_EQ(took->getMustPlay(), std::nullopt);
  EXPECT_EQ(took->getDiscard().size(), 3u);
}

TEST(TakeDown, TakesEveryCardFromTheTopDownToTheNamedOne) {
  auto took = aliceToDraw().drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  EXPECT_EQ(took->getDiscard(), vector<Card>{c(Rank::Two)});
  // The hand gains them as they lay in the pile, the deepest first.
  EXPECT_EQ(took->getPlayer(0).hand,
            (vector<Card>{c(Rank::Seven, Suit::Hearts), c(Rank::Nine, Suit::Diamonds),
                          c(Rank::Jack), c(Rank::Five, Suit::Hearts), c(Rank::Six, Suit::Hearts),
                          c(Rank::King, Suit::Spades)}));
  EXPECT_EQ(took->getStage(), Stage::Play);
  EXPECT_EQ(took->getMustPlay(), c(Rank::Five, Suit::Hearts));
  // The one-card rule is for a one-card draw: the must-play binds instead.
  EXPECT_EQ(took->getTakenDiscard(), std::nullopt);
  EXPECT_EQ(took->getLastMove()->kind, MoveKind::DrawDiscard);
  EXPECT_EQ(took->getLastMove()->cards,
            (vector<Card>{c(Rank::Five, Suit::Hearts), c(Rank::Six, Suit::Hearts),
                          c(Rank::King, Suit::Spades)}));
}

TEST(TakeDown, TheWholePileIfItsBottomCardPlays) {
  // 2♣ plays with the 3♣ and 4♣ she holds.
  auto took = aliceToDraw({}, {c(Rank::Three), c(Rank::Four)}).drawDiscard(0, c(Rank::Two));
  ASSERT_TRUE(took.ok()) << took.status();
  EXPECT_TRUE(took->getDiscard().empty());
  EXPECT_EQ(took->getPlayer(0).hand.size(), 6u);
}

TEST(TakeDown, ACardNotInThePileIsRefused) {
  const absl::Status refused = aliceToDraw().drawDiscard(0, c(Rank::Ace)).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(refused.message(), "that card is not in the discard pile");
}

TEST(TakeDown, NotDownToACardThatCannotBePlayed) {
  // Nothing melds with the 2♣, and no table meld takes it.
  const absl::Status refused = aliceToDraw().drawDiscard(0, c(Rank::Two)).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "you could not play the 2♣");
  // The 6♥ alone has only the 7♥: a pair is no meld, and the 5♥ stays below.
  EXPECT_EQ(aliceToDraw().drawDiscard(0, c(Rank::Six, Suit::Hearts)).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(TakeDown, PlayableByLayingOffOntoATableMeld) {
  const vector<Meld> sixes = {
      {"bob", {c(Rank::Six), c(Rank::Six, Suit::Diamonds), c(Rank::Six, Suit::Spades)}}};
  EXPECT_TRUE(aliceToDraw(sixes).drawDiscard(0, c(Rank::Six, Suit::Hearts)).ok());
}

TEST(TakeDown, PlayableByLayingOffAfterCardsThatBridgeTheGap) {
  // 2♣ onto bob's 5-6-7♣ needs the 3♣ and 4♣ first; she holds the 4♣ and
  // the 3♣ is nowhere, so no. With the 3♣ in hand, yes.
  const vector<Meld> run = {{"bob", {c(Rank::Five), c(Rank::Six), c(Rank::Seven)}}};
  EXPECT_EQ(aliceToDraw(run, {c(Rank::Four), c(Rank::Nine, Suit::Diamonds)})
                .drawDiscard(0, c(Rank::Two))
                .status()
                .code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(aliceToDraw(run, {c(Rank::Four), c(Rank::Three, Suit::Clubs)})
                  .drawDiscard(0, c(Rank::Two))
                  .ok());
}

TEST(TakeDown, TheTurnCannotEndUntilTheDeepestCardIsPlayed) {
  auto took = aliceToDraw().drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok());
  const absl::Status refused = took->discard(0, c(Rank::King, Suit::Spades)).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "play the 5♥ you took first");
}

TEST(TakeDown, AMeldWithoutTheDeepestCardLeavesItOwed) {
  auto took =
      aliceToDraw({}, {c(Rank::Seven, Suit::Hearts), c(Rank::King), c(Rank::King, Suit::Diamonds)})
          .drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  auto kings =
      took->meld(0, {c(Rank::King), c(Rank::King, Suit::Diamonds), c(Rank::King, Suit::Spades)});
  ASSERT_TRUE(kings.ok()) << kings.status();
  EXPECT_EQ(kings->getMustPlay(), c(Rank::Five, Suit::Hearts));
  EXPECT_FALSE(kings->discard(0, c(Rank::Seven, Suit::Hearts)).ok());
}

TEST(TakeDown, MeldingTheDeepestCardSettlesIt) {
  auto took = aliceToDraw().drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok());
  auto melded = took->meld(
      0, {c(Rank::Five, Suit::Hearts), c(Rank::Six, Suit::Hearts), c(Rank::Seven, Suit::Hearts)});
  ASSERT_TRUE(melded.ok()) << melded.status();
  EXPECT_EQ(melded->getMustPlay(), std::nullopt);
  // And the top card taken may go straight back: only a one-card draw
  // holds its card.
  EXPECT_TRUE(melded->discard(0, c(Rank::King, Suit::Spades)).ok());
}

TEST(TakeDown, LayingTheDeepestCardOffSettlesIt) {
  const vector<Meld> sixes = {
      {"bob", {c(Rank::Six), c(Rank::Six, Suit::Diamonds), c(Rank::Six, Suit::Spades)}}};
  auto took = aliceToDraw(sixes).drawDiscard(0, c(Rank::Six, Suit::Hearts));
  ASSERT_TRUE(took.ok());
  auto laid = took->layOff(0, c(Rank::Six, Suit::Hearts), 0);
  ASSERT_TRUE(laid.ok()) << laid.status();
  EXPECT_EQ(laid->getMustPlay(), std::nullopt);
  EXPECT_TRUE(laid->discard(0, c(Rank::Nine, Suit::Diamonds)).ok());
}

TEST(TakeDown, NoMeldThatLeavesTheDeepestCardUnplayable) {
  // Down to the 5♥ on the strength of the 7♥; melding the 7♥ into a set
  // instead would strand the 5♥ and wedge the turn.
  auto took = aliceToDraw({}, {c(Rank::Seven, Suit::Hearts), c(Rank::Seven),
                               c(Rank::Seven, Suit::Diamonds)})
                  .drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  const absl::Status refused =
      took->meld(0, {c(Rank::Seven), c(Rank::Seven, Suit::Diamonds), c(Rank::Seven, Suit::Hearts)})
          .status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "that would leave the 5♥ you took unplayable");
}

TEST(TakeDown, NoLayOffThatLeavesTheDeepestCardUnplayable) {
  const vector<Meld> sevens = {
      {"bob", {c(Rank::Seven), c(Rank::Seven, Suit::Diamonds), c(Rank::Seven, Suit::Spades)}}};
  auto took = aliceToDraw(sevens).drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  const absl::Status refused = took->layOff(0, c(Rank::Seven, Suit::Hearts), 0).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "that would leave the 5♥ you took unplayable");
}

TEST(TakeDown, ALayOffThatKeepsTheDeepestCardPlayableIsAllowed) {
  // The 7♥ onto bob's 8-9-10♥ leaves the 6♥ and then the 5♥ to lay off.
  const vector<Meld> hearts = {
      {"bob",
       {c(Rank::Eight, Suit::Hearts), c(Rank::Nine, Suit::Hearts), c(Rank::Ten, Suit::Hearts)}}};
  auto took = aliceToDraw(hearts).drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  auto laid = took->layOff(0, c(Rank::Seven, Suit::Hearts), 0);
  ASSERT_TRUE(laid.ok()) << laid.status();
  EXPECT_EQ(laid->getMustPlay(), c(Rank::Five, Suit::Hearts));
}

TEST(TakeDown, TheCardsASeatMayTakeDownTo) {
  // The top always; the 5♥, which melds with the 6♥ above it and the 7♥ in
  // hand; not the 6♥ or the 2♣.
  EXPECT_EQ(aliceToDraw().discardTakeable(0),
            (vector<Card>{c(Rank::Five, Suit::Hearts), c(Rank::King, Suit::Spades)}));
  // Only on its draw.
  EXPECT_TRUE(aliceToDraw().discardTakeable(1).empty());
  auto took = aliceToDraw().drawDiscard(0);
  ASSERT_TRUE(took.ok());
  EXPECT_TRUE(took->discardTakeable(0).empty());
}

TEST(TakeDown, DownToACardThatMakesASetWithTwoHeld) {
  // The 2♣ with the 2♦ and 2♥ in hand: the card taken down to counts
  // toward its own meld.
  const GameState twos =
      aliceToDraw({}, {c(Rank::Two, Suit::Diamonds), c(Rank::Two, Suit::Hearts)});
  EXPECT_EQ(twos.discardTakeable(0).front(), c(Rank::Two));
  EXPECT_TRUE(twos.drawDiscard(0, c(Rank::Two)).ok());
}

TEST(TakeDown, ASeatLeavingOwingACardTakesTheDebtWithIt) {
  GameState three =
      playing({{"alice", {c(Rank::Seven, Suit::Hearts), c(Rank::Nine, Suit::Diamonds)}},
               {"bob", {c(Rank::Three, Suit::Spades), c(Rank::Eight, Suit::Spades)}},
               {"carol", {c(Rank::Ten, Suit::Spades)}}},
              {c(Rank::Four, Suit::Spades), c(Rank::Queen, Suit::Diamonds)},
              {c(Rank::Two), c(Rank::Five, Suit::Hearts), c(Rank::Six, Suit::Hearts),
               c(Rank::King, Suit::Spades)},
              0, Stage::Draw);
  auto took = three.drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  auto left = took->removePlayer(0);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_EQ(left->getMustPlay(), std::nullopt);
  auto drew = left->drawStock(0);
  ASSERT_TRUE(drew.ok()) << drew.status();
  EXPECT_TRUE(drew->discard(0, c(Rank::Three, Suit::Spades)).ok());
}

TEST(TakeDown, ADealAbandonedOwingACardOwesNothing) {
  auto took = aliceToDraw().drawDiscard(0, c(Rank::Five, Suit::Hearts));
  ASSERT_TRUE(took.ok()) << took.status();
  for (int leaver : {0, 1}) {
    auto left = took->removePlayer(leaver);
    ASSERT_TRUE(left.ok()) << left.status();
    EXPECT_EQ(left->getPhase(), Phase::Abandoned);
    EXPECT_EQ(left->getMustPlay(), std::nullopt) << leaver;
  }
}
