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
