#include "domains/games/libs/cards/castle/game_state.h"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/player.h"
#include "domains/games/libs/cards/dealer.h"

using namespace cards;
using namespace castle;
using std::deque;
using std::string;
using std::vector;

namespace {

Card c(Rank rank, Suit suit = Suit::Clubs) { return Card{suit, rank}; }

Player seat(const string& id, vector<Card> hand, vector<Card> faceUp = {},
            vector<Card> faceDown = {}) {
  return Player{id, std::move(hand), std::move(faceUp), std::move(faceDown), true};
}

/// A game in play: seats as given, the pile's back on top, seat `turn` to move.
GameState playing(vector<Player> players, vector<Card> pile = {}, deque<Card> draw = {},
                  int turn = 0, std::optional<LastPlay> lastPlay = std::nullopt) {
  return GameState{std::move(draw),
                   std::move(pile),
                   std::move(players),
                   turn,
                   Phase::Playing,
                   {},
                   "game",
                   "v0",
                   std::move(lastPlay)};
}

}  // namespace

TEST(Deal, NineCardsASeatFromTheBackOfTheDeckIntoSetup) {
  NoShuffleDealer dealer;
  auto game = dealCastleGame("g1", {"a", "b"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(game.ok());
  EXPECT_EQ(game->getPhase(), Phase::Setup);
  EXPECT_EQ(game->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_EQ(game->getGameId(), "g1");
  EXPECT_EQ(game->getDrawPile().size(), 52u - 18u);
  EXPECT_TRUE(game->getPile().empty());
  ASSERT_EQ(game->getPlayers().size(), 2u);
  for (const Player& p : game->getPlayers()) {
    EXPECT_EQ(p.getHand().size(), 3u);
    EXPECT_EQ(p.getFaceUp().size(), 3u);
    EXPECT_EQ(p.getFaceDown().size(), 3u);
    EXPECT_FALSE(p.isReady());
  }
  // The unshuffled deck ends in the four aces, then kings: seat a's
  // face-down row is dealt first, from the back, then face-up, then hand.
  EXPECT_EQ(game->getPlayer(0).getFaceDown().at(0), c(Rank::Ace, Suit::Spades));
  EXPECT_EQ(game->getPlayer(0).getFaceUp().at(0), c(Rank::Ace, Suit::Clubs));
  EXPECT_EQ(game->getPlayer(0).getHand().at(0), c(Rank::King, Suit::Diamonds));
  EXPECT_EQ(game->getPlayer(1).getFaceDown().at(0), c(Rank::Queen, Suit::Hearts));
  EXPECT_EQ(game->playerIndex("b"), 1);
  EXPECT_EQ(game->playerIndex("zed"), -1);
}

TEST(Deal, RejectsTheWrongTableOrAShortDeck) {
  NoShuffleDealer dealer;
  EXPECT_FALSE(dealCastleGame("g", {"a"}, dealer.DealNewUnshuffledDeck()).ok());
  EXPECT_FALSE(dealCastleGame("g", {"a", "b", "c", "d", "e"}, dealer.DealNewUnshuffledDeck()).ok());
  EXPECT_TRUE(dealCastleGame("g", {"a", "b", "c", "d"}, dealer.DealNewUnshuffledDeck()).ok());

  deque<Card> seventeen;
  for (int i = 0; i < 17; i++) {
    seventeen.emplace_back(i);
  }
  EXPECT_FALSE(dealCastleGame("g", {"a", "b"}, seventeen).ok());
  seventeen.emplace_back(17);
  auto exact = dealCastleGame("g", {"a", "b"}, seventeen);
  ASSERT_TRUE(exact.ok());
  EXPECT_TRUE(exact->getDrawPile().empty());
}

TEST(Setup, SwapsThenReadyOpensPlayWhenEveryoneIsReady) {
  NoShuffleDealer dealer;
  auto game = dealCastleGame("g", {"a", "b"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(game.ok());
  const Card handCard = game->getPlayer(0).getHand().at(0);
  const Card tableCard = game->getPlayer(0).getFaceUp().at(1);

  auto swapped = game->swapForSetup(0, 0, 1);
  ASSERT_TRUE(swapped.ok());
  EXPECT_EQ(swapped->getPlayer(0).getHand().at(0), tableCard);
  EXPECT_EQ(swapped->getPlayer(0).getFaceUp().at(1), handCard);
  EXPECT_EQ(swapped->getPlayer(1), game->getPlayer(1));
  EXPECT_FALSE(swapped->swapForSetup(0, 3, 0).ok());
  EXPECT_FALSE(swapped->swapForSetup(2, 0, 0).ok());

  // No turn moves during setup.
  EXPECT_FALSE(swapped->playFromHand(0, {0}).ok());
  EXPECT_FALSE(swapped->pickUp(0).ok());

  auto aReady = swapped->ready(0);
  ASSERT_TRUE(aReady.ok());
  EXPECT_EQ(aReady->getPhase(), Phase::Setup);
  EXPECT_FALSE(aReady->swapForSetup(0, 0, 0).ok());  // locked in
  EXPECT_FALSE(aReady->ready(0).ok());
  EXPECT_TRUE(aReady->swapForSetup(1, 0, 0).ok());  // b is still arranging

  auto open = aReady->ready(1);
  ASSERT_TRUE(open.ok());
  EXPECT_EQ(open->getPhase(), Phase::Playing);
  EXPECT_NE(open->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_FALSE(open->ready(0).ok());
  EXPECT_FALSE(open->swapForSetup(1, 0, 0).ok());
}

TEST(Setup, TheLowestOrdinaryHandCardOpensTiesToTheEarliestSeat) {
  const Player a{"a", {c(Rank::Two), c(Rank::Ten), c(Rank::King)}, {}, {}, true};
  const Player b{"b", {c(Rank::Ace), c(Rank::Five), c(Rank::Ace)}, {}, {}, true};
  const Player cc{"c", {c(Rank::Five), c(Rank::Nine), c(Rank::Nine)}, {}, {}, false};
  GameState setup{{}, {}, {a, b, cc}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto open = setup.ready(2);
  ASSERT_TRUE(open.ok());
  EXPECT_EQ(open->getWhoseTurn(), 1);  // b's five beats a's specials; b before c
}

TEST(Setup, ATableWithNoOrdinaryHandCardOpensAtSeatZero) {
  const Player a{"a", {c(Rank::Ten), c(Rank::Ten)}, {}, {}, true};
  const Player b{"b", {c(Rank::Two), c(Rank::Ten)}, {}, {}, false};
  GameState specials{{}, {}, {a, b}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto opened = specials.ready(1);
  ASSERT_TRUE(opened.ok());
  EXPECT_EQ(opened->getWhoseTurn(), 0);
}

TEST(Setup, OnlyHandCardsDecideTheOpeningSeat) {
  const Player a{"a", {c(Rank::Nine)}, {c(Rank::Three)}, {}, true};
  const Player b{"b", {c(Rank::Five)}, {c(Rank::King)}, {}, false};
  GameState setup{{}, {}, {a, b}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto opened = setup.ready(1);
  ASSERT_TRUE(opened.ok());
  EXPECT_EQ(opened->getWhoseTurn(), 1);  // a's three is on the table, not in hand
}

TEST(Play, ACardGoesOnAPileTopOfItsRankOrLower) {
  const GameState g = playing(
      {seat("a", {c(Rank::Five), c(Rank::Seven), c(Rank::Nine)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Seven)});
  EXPECT_TRUE(g.isPlayable(Rank::Seven));
  EXPECT_TRUE(g.isPlayable(Rank::Nine));
  EXPECT_FALSE(g.isPlayable(Rank::Five));
  EXPECT_TRUE(g.hasLegalPlay(0));
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());

  auto next = g.playFromHand(0, {1});
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(next->getPile(), (vector<Card>{c(Rank::Seven), c(Rank::Seven)}));
  EXPECT_EQ(next->getPlayer(0).getHand(), (vector<Card>{c(Rank::Five), c(Rank::Nine)}));
  EXPECT_EQ(next->getWhoseTurn(), 1);
  EXPECT_EQ(next->getPhase(), Phase::Playing);
}

TEST(Play, OnlyTheSeatWhoseTurnItIsMayMove) {
  const GameState g = playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Nine)})}, {}, {}, 1);
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());
  EXPECT_FALSE(g.pickUp(0).ok());
  EXPECT_TRUE(g.playFromHand(1, {0}).ok());
}

TEST(Play, ASeatOutsideTheTableIsRejectedEverywhere) {
  const GameState g = playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Nine)})});
  for (int bad : {-1, 2, 5}) {
    EXPECT_FALSE(g.playFromHand(bad, {0}).ok());
    EXPECT_FALSE(g.playFaceUp(bad, {0}).ok());
    EXPECT_FALSE(g.playFaceDown(bad, 0).ok());
    EXPECT_FALSE(g.pickUp(bad).ok());
    EXPECT_FALSE(g.ready(bad).ok());
    EXPECT_FALSE(g.swapForSetup(bad, 0, 0).ok());
    EXPECT_FALSE(g.removePlayer(bad).ok());
    EXPECT_FALSE(g.hasLegalPlay(bad));
  }
}

TEST(Play, ATwoResetsThePileAndATenBurnsIt) {
  const GameState g =
      playing({seat("a", {c(Rank::Two), c(Rank::Three), c(Rank::Ten)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::King)});
  EXPECT_TRUE(g.isPlayable(Rank::Two));
  EXPECT_TRUE(g.isPlayable(Rank::Ten));
  EXPECT_FALSE(g.isPlayable(Rank::Three));

  auto reset = g.playFromHand(0, {0});
  ASSERT_TRUE(reset.ok());
  EXPECT_EQ(reset->pileTop(), c(Rank::Two));
  EXPECT_TRUE(reset->isPlayable(Rank::Three));
  EXPECT_EQ(reset->getWhoseTurn(), 1);

  auto burn = g.playFromHand(0, {2});
  ASSERT_TRUE(burn.ok());
  EXPECT_TRUE(burn->getPile().empty());
  EXPECT_EQ(burn->pileTop(), std::nullopt);
  EXPECT_EQ(burn->getWhoseTurn(), 0);  // the burner goes again
  EXPECT_TRUE(burn->isPlayable(Rank::Three));
  EXPECT_FALSE(burn->pickUp(0).ok());  // nothing to pick up
}

TEST(Play, FourOfAKindOnTopBurnsAcrossPlays) {
  const GameState g = playing(
      {seat("a", {c(Rank::Eight), c(Rank::Eight), c(Rank::Four)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Eight, Suit::Hearts), c(Rank::Eight, Suit::Spades)});
  auto burn = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(burn.ok());
  EXPECT_TRUE(burn->getPile().empty());
  EXPECT_EQ(burn->getWhoseTurn(), 0);

  const GameState split = playing(
      {seat("a", {c(Rank::Eight), c(Rank::Eight), c(Rank::Four)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Eight, Suit::Hearts), c(Rank::Eight, Suit::Spades), c(Rank::Eight, Suit::Diamonds),
       c(Rank::Seven)});
  // Three eights buried under a seven and two more on top is not a run of four.
  auto noBurn = split.playFromHand(0, {0, 1});
  ASSERT_TRUE(noBurn.ok());
  EXPECT_EQ(noBurn->getPile().size(), 6u);
  EXPECT_EQ(noBurn->getWhoseTurn(), 1);
}

TEST(Play, FourTwosBurnLikeAnyOtherFourOfAKind) {
  const GameState g =
      playing({seat("a", {c(Rank::Two), c(Rank::Two, Suit::Hearts), c(Rank::Four)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::King), c(Rank::Two, Suit::Spades), c(Rank::Two, Suit::Diamonds)});
  auto burn = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(burn.ok());
  EXPECT_TRUE(burn->getPile().empty());
  EXPECT_EQ(burn->getWhoseTurn(), 0);
}

TEST(Play, AFourOfAKindRunBrokenByATwoDoesNotBurn) {
  const GameState g =
      playing({seat("a", {c(Rank::Eight), c(Rank::Eight, Suit::Hearts), c(Rank::Four)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::Eight, Suit::Spades), c(Rank::Eight, Suit::Diamonds), c(Rank::Two)});
  auto pair = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(pair.ok());
  EXPECT_EQ(pair->getPile().size(), 5u);
  EXPECT_EQ(pair->getWhoseTurn(), 1);
}

TEST(Play, ABurnMustStillBeAPlayableRank) {
  const GameState g =
      playing({seat("a", {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades),
                          c(Rank::Five, Suit::Diamonds)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::King)});
  EXPECT_FALSE(g.playFromHand(0, {0, 1, 2, 3}).ok());
}

TEST(Play, AHandNeverEmptiesWhileTheDrawPileLasts) {
  const GameState g =
      playing({seat("a", {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades)},
                    {c(Rank::Nine)}),
               seat("b", {c(Rank::Nine)})},
              {}, {c(Rank::Jack)});
  auto played = g.playFromHand(0, {0, 1, 2});
  ASSERT_TRUE(played.ok());
  EXPECT_EQ(played->getPlayer(0).getHand(), (vector<Card>{c(Rank::Jack)}));
  EXPECT_EQ(played->getPlayer(0).source(), Source::Hand);
  EXPECT_TRUE(played->getFinished().empty());
}

TEST(Play, AHandPlayDrawsBackUpToThreeWhileTheDrawPileLasts) {
  const GameState g = playing(
      {seat("a", {c(Rank::Five), c(Rank::Five), c(Rank::Five)}), seat("b", {c(Rank::Nine)})}, {},
      {c(Rank::Jack), c(Rank::Queen)});
  auto one = g.playFromHand(0, {1});
  ASSERT_TRUE(one.ok());
  EXPECT_EQ(one->getPlayer(0).getHand(),
            (vector<Card>{c(Rank::Five), c(Rank::Five), c(Rank::Queen)}));
  EXPECT_EQ(one->getDrawPile(), (deque<Card>{c(Rank::Jack)}));

  auto three = g.playFromHand(0, {0, 1, 2});
  ASSERT_TRUE(three.ok());
  EXPECT_EQ(three->getPlayer(0).getHand(), (vector<Card>{c(Rank::Queen), c(Rank::Jack)}));
  EXPECT_TRUE(three->getDrawPile().empty());
  EXPECT_EQ(three->getPile().size(), 3u);
}

TEST(Play, OneRankPerPlayFromRealCards) {
  const GameState g =
      playing({seat("a", {c(Rank::Five), c(Rank::Six), c(Rank::Six)}), seat("b", {c(Rank::Nine)})});
  EXPECT_FALSE(g.playFromHand(0, {0, 1}).ok());
  EXPECT_FALSE(g.playFromHand(0, {}).ok());
  EXPECT_FALSE(g.playFromHand(0, {1, 1}).ok());
  EXPECT_FALSE(g.playFromHand(0, {3}).ok());
  EXPECT_FALSE(g.playFromHand(0, {-1}).ok());
  auto pair = g.playFromHand(0, {1, 2});
  ASSERT_TRUE(pair.ok());
  EXPECT_EQ(pair->getPile(), (vector<Card>{c(Rank::Six), c(Rank::Six)}));
  EXPECT_EQ(g.getPlayer(0).getHand().size(), 3u);  // the source state is untouched
}

TEST(Play, ASeatWithNoPlayablePickUpTakesThePileWithoutDrawing) {
  const GameState stuck =
      playing({seat("a", {c(Rank::Three), c(Rank::Four)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Five), c(Rank::Seven)}, {c(Rank::Ace)});
  EXPECT_FALSE(stuck.hasLegalPlay(0));
  auto took = stuck.pickUp(0);
  ASSERT_TRUE(took.ok());
  EXPECT_EQ(took->getPlayer(0).getHand(),
            (vector<Card>{c(Rank::Three), c(Rank::Four), c(Rank::Five), c(Rank::Seven)}));
  EXPECT_TRUE(took->getPile().empty());
  EXPECT_EQ(took->getDrawPile(), (deque<Card>{c(Rank::Ace)}));
  EXPECT_EQ(took->getWhoseTurn(), 1);

  const GameState able = playing(
      {seat("a", {c(Rank::Three), c(Rank::Nine)}), seat("b", {c(Rank::Nine)})}, {c(Rank::Seven)});
  EXPECT_FALSE(able.pickUp(0).ok());  // the nine must be played
  EXPECT_FALSE(able.pickUp(1).ok());  // not b's turn
}

TEST(Play, TheFaceUpRowIsInPlayOnceTheHandIsEmpty) {
  const GameState g = playing(
      {seat("a", {}, {c(Rank::Nine), c(Rank::Nine), c(Rank::Two)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Seven)});
  EXPECT_TRUE(g.hasLegalPlay(0));
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());
  auto pair = g.playFaceUp(0, {0, 1});
  ASSERT_TRUE(pair.ok());
  EXPECT_EQ(pair->getPlayer(0).getFaceUp(), (vector<Card>{c(Rank::Two)}));
  EXPECT_EQ(pair->getPile().size(), 3u);
  EXPECT_EQ(pair->getWhoseTurn(), 1);

  const GameState handFirst =
      playing({seat("a", {c(Rank::Nine)}, {c(Rank::Nine)}), seat("b", {c(Rank::Nine)})});
  EXPECT_FALSE(handFirst.playFaceUp(0, {0}).ok());

  // Picking up from the face-up row puts the pile in the hand, and the
  // hand is the row in play again.
  const GameState stuck =
      playing({seat("a", {}, {c(Rank::Three)}), seat("b", {c(Rank::Nine)})}, {c(Rank::King)});
  auto took = stuck.pickUp(0);
  ASSERT_TRUE(took.ok());
  EXPECT_EQ(took->getPlayer(0).getHand(), (vector<Card>{c(Rank::King)}));
  EXPECT_EQ(took->getPlayer(0).getFaceUp(), (vector<Card>{c(Rank::Three)}));
  EXPECT_EQ(took->getPlayer(0).source(), Source::Hand);
}

TEST(Play, FaceDownCardsPlayBlindAndAnUnplayableOneIsPickedUpWithThePile) {
  const GameState g =
      playing({seat("a", {}, {}, {c(Rank::Three), c(Rank::King)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Seven)});
  EXPECT_FALSE(g.hasLegalPlay(0));  // blind rows never count as a legal play
  EXPECT_FALSE(g.pickUp(0).ok());
  EXPECT_FALSE(g.playFaceUp(0, {0}).ok());
  EXPECT_FALSE(g.playFaceDown(0, 2).ok());

  auto lucky = g.playFaceDown(0, 1);
  ASSERT_TRUE(lucky.ok());
  EXPECT_EQ(lucky->pileTop(), c(Rank::King));
  EXPECT_EQ(lucky->getPlayer(0).getFaceDown(), (vector<Card>{c(Rank::Three)}));
  EXPECT_EQ(lucky->getWhoseTurn(), 1);

  auto unlucky = g.playFaceDown(0, 0);
  ASSERT_TRUE(unlucky.ok());
  EXPECT_TRUE(unlucky->getPile().empty());
  EXPECT_EQ(unlucky->getPlayer(0).getHand(), (vector<Card>{c(Rank::Seven), c(Rank::Three)}));
  EXPECT_EQ(unlucky->getPlayer(0).getFaceDown(), (vector<Card>{c(Rank::King)}));
  EXPECT_EQ(unlucky->getWhoseTurn(), 1);
  EXPECT_EQ(unlucky->getPhase(), Phase::Playing);
}

TEST(Play, TheFaceDownRowIsClosedWhileAnotherRowHasCards) {
  const GameState hand =
      playing({seat("a", {c(Rank::Nine)}, {}, {c(Rank::Ace)}), seat("b", {c(Rank::Nine)})});
  EXPECT_FALSE(hand.playFaceDown(0, 0).ok());
  const GameState table =
      playing({seat("a", {}, {c(Rank::Nine)}, {c(Rank::Ace)}), seat("b", {c(Rank::Nine)})});
  EXPECT_FALSE(table.playFaceDown(0, 0).ok());
}

TEST(Play, AFaceDownCardOnAnEmptyPileAlwaysPlays) {
  const GameState g = playing({seat("a", {}, {}, {c(Rank::Three)}), seat("b", {c(Rank::Nine)})});
  auto flipped = g.playFaceDown(0, 0);
  ASSERT_TRUE(flipped.ok());
  EXPECT_EQ(flipped->pileTop(), c(Rank::Three));
  EXPECT_TRUE(flipped->getPlayer(0).getHand().empty());
}

TEST(Play, ATenAsTheLastFaceDownCardBurnsAndGoesOut) {
  const GameState g = playing(
      {seat("a", {}, {}, {c(Rank::Ten)}), seat("b", {c(Rank::Nine)}), seat("c", {c(Rank::Nine)})},
      {c(Rank::Seven)});
  auto out = g.playFaceDown(0, 0);
  ASSERT_TRUE(out.ok());
  EXPECT_TRUE(out->getPile().empty());
  EXPECT_EQ(out->getFinished(), (vector<string>{"a"}));
  // Out on a burn: no second play for the burner, the game is over.
  EXPECT_EQ(out->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_EQ(out->getPhase(), Phase::Over);
  EXPECT_EQ(out->loser(), std::nullopt);  // two seats still hold cards
}

// The run on top sets the price: the last play was n cards of rank k,
// so the next is n or more of rank k or higher.
TEST(Runs, APlayMustMatchOrBeatTheCountOnTop) {
  const GameState g = playing({seat("a", {c(Rank::Seven), c(Rank::Seven, Suit::Hearts),
                                          c(Rank::Seven, Suit::Spades), c(Rank::Nine)}),
                               seat("b", {c(Rank::Nine)})},
                              {c(Rank::Five), c(Rank::Five, Suit::Hearts)});
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());  // one seven on a pair of fives
  EXPECT_FALSE(g.playFromHand(0, {3}).ok());  // one nine, same
  auto pair = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(pair.ok()) << pair.status();
  EXPECT_EQ(pair->getPile().size(), 4u);
  EXPECT_EQ(pair->getWhoseTurn(), 1);
  auto triple = g.playFromHand(0, {0, 1, 2});  // more than the count is fine
  ASSERT_TRUE(triple.ok()) << triple.status();
  EXPECT_EQ(triple->getPile().size(), 5u);
}

TEST(Runs, ATripleOnTopNeedsATriple) {
  const GameState g =
      playing({seat("a", {c(Rank::Nine), c(Rank::Nine, Suit::Hearts), c(Rank::Nine, Suit::Spades)}),
               seat("b", {c(Rank::Nine, Suit::Diamonds)})},
              {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades)});
  EXPECT_FALSE(g.playFromHand(0, {0, 1}).ok());
  ASSERT_TRUE(g.playFromHand(0, {0, 1, 2}).ok());
}

// Fewer cards of the top's own rank are legal exactly when they complete
// the four of a kind, which burns and hands the turn back.
TEST(Runs, FewerOfTheSameRankCompleteFourAndBurn) {
  const GameState triple = playing(
      {seat("a", {c(Rank::Five, Suit::Diamonds), c(Rank::Nine)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades)});
  auto burn = triple.playFromHand(0, {0});
  ASSERT_TRUE(burn.ok()) << burn.status();
  EXPECT_TRUE(burn->getPile().empty());
  EXPECT_EQ(burn->getWhoseTurn(), 0);

  const GameState pair = playing(
      {seat("a", {c(Rank::Five, Suit::Diamonds), c(Rank::Nine)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::Five), c(Rank::Five, Suit::Hearts)});
  EXPECT_FALSE(pair.playFromHand(0, {0}).ok());  // three of a kind is not a completion
}

TEST(Runs, SpecialsIgnoreTheCount) {
  const GameState g =
      playing({seat("a", {c(Rank::Two), c(Rank::Ten), c(Rank::Nine)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Five), c(Rank::Five, Suit::Hearts)});
  auto reset = g.playFromHand(0, {0});
  ASSERT_TRUE(reset.ok()) << reset.status();
  EXPECT_EQ(reset->pileTop(), c(Rank::Two));
  auto burn = g.playFromHand(0, {1});
  ASSERT_TRUE(burn.ok()) << burn.status();
  EXPECT_TRUE(burn->getPile().empty());
}

TEST(Runs, ABlindCardMustMeetTheCountOrCompleteTheFour) {
  const GameState g = playing({seat("a", {}, {}, {c(Rank::Seven)}), seat("b", {c(Rank::Nine)})},
                              {c(Rank::Five), c(Rank::Five, Suit::Hearts)});
  auto pickedUp = g.playFaceDown(0, 0);
  ASSERT_TRUE(pickedUp.ok()) << pickedUp.status();
  EXPECT_TRUE(pickedUp->getPile().empty());
  EXPECT_EQ(pickedUp->getPlayer(0).getHand().size(), 3u);
  EXPECT_EQ(pickedUp->getWhoseTurn(), 1);

  const GameState completes =
      playing({seat("a", {}, {}, {c(Rank::Five, Suit::Diamonds), c(Rank::Nine)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades)});
  auto burn = completes.playFaceDown(0, 0);
  ASSERT_TRUE(burn.ok()) << burn.status();
  EXPECT_TRUE(burn->getPile().empty());
  EXPECT_EQ(burn->getWhoseTurn(), 0);
}

// The burner plays again from whichever row is next: the hand while it
// has cards, then the face-up row, then blind.
TEST(Runs, ABurnLetsTheSameSeatPlayItsNextRow) {
  const GameState g = playing(
      {seat("a", {c(Rank::Ten)}, {c(Rank::Nine)}, {c(Rank::Four)}), seat("b", {c(Rank::Nine)})},
      {c(Rank::King)});
  auto burn = g.playFromHand(0, {0});
  ASSERT_TRUE(burn.ok()) << burn.status();
  EXPECT_EQ(burn->getWhoseTurn(), 0);
  EXPECT_EQ(burn->getPlayer(0).source(), Source::FaceUp);
  auto faceUp = burn->playFaceUp(0, {0});
  ASSERT_TRUE(faceUp.ok()) << faceUp.status();
  EXPECT_EQ(faceUp->getWhoseTurn(), 1);
  EXPECT_EQ(faceUp->getPlayer(0).source(), Source::FaceDown);
}

TEST(Runs, NoPlayThatMeetsTheCountMeansPickUp) {
  const GameState g =
      playing({seat("a", {c(Rank::King), c(Rank::Ace)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Nine), c(Rank::Nine, Suit::Hearts)});
  EXPECT_FALSE(g.hasLegalPlay(0));
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());
  auto pickedUp = g.pickUp(0);
  ASSERT_TRUE(pickedUp.ok()) << pickedUp.status();
  EXPECT_EQ(pickedUp->getPlayer(0).getHand().size(), 4u);

  const GameState pairInHand =
      playing({seat("a", {c(Rank::King), c(Rank::King, Suit::Hearts)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Nine), c(Rank::Nine, Suit::Hearts)});
  EXPECT_TRUE(pairInHand.hasLegalPlay(0));
  EXPECT_FALSE(pairInHand.pickUp(0).ok());
}

// The first seat to shed its last card wins, and that ends the game: no
// play-on to a single loser. Only a two-seat game has a loser to name.
// A two on top is the lowest rank, so it asks only for its count.
TEST(Runs, APairOfTwosOnTopStillSetsTheCount) {
  const GameState g =
      playing({seat("a", {c(Rank::King), c(Rank::King, Suit::Hearts), c(Rank::Nine)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::Two), c(Rank::Two, Suit::Hearts)});
  EXPECT_EQ(g.runOnTop(), 2);
  EXPECT_FALSE(g.isPlayable(Rank::King));
  EXPECT_FALSE(g.playFromHand(0, {0}).ok());
  auto pair = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(pair.ok()) << pair.status();
  EXPECT_EQ(pair->getPile().size(), 4u);
  EXPECT_EQ(pair->getWhoseTurn(), 1);
}

TEST(Runs, SpecialsPlayInAnyCount) {
  const GameState g =
      playing({seat("a", {c(Rank::Two), c(Rank::Two, Suit::Hearts), c(Rank::Nine)}),
               seat("b", {c(Rank::Nine)})},
              {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Five, Suit::Spades)});
  EXPECT_TRUE(g.isPlayable(Rank::Two, 2));
  auto pair = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(pair.ok()) << pair.status();
  EXPECT_EQ(pair->runOnTop(), 2);
  EXPECT_EQ(pair->getWhoseTurn(), 1);
}

TEST(Runs, AnEmptyPileHasNoRunAndNoCardsIsNoPlay) {
  const GameState g = playing({seat("a", {c(Rank::Three)}), seat("b", {c(Rank::Nine)})});
  EXPECT_EQ(g.runOnTop(), 0);
  EXPECT_EQ(g.pileTop(), std::nullopt);
  EXPECT_TRUE(g.isPlayable(Rank::Three, 1));
  EXPECT_FALSE(g.isPlayable(Rank::Three, 0));
  EXPECT_FALSE(g.isPlayable(Rank::Three, -1));
}

TEST(Play, OnlyAHandPlayDrawsBackUp) {
  const GameState g =
      playing({seat("a", {}, {c(Rank::Nine)}, {c(Rank::Four)}), seat("b", {c(Rank::Nine)})},
              {c(Rank::Seven)}, {c(Rank::Jack), c(Rank::Queen)});
  auto played = g.playFaceUp(0, {0});
  ASSERT_TRUE(played.ok()) << played.status();
  EXPECT_TRUE(played->getPlayer(0).getHand().empty());
  EXPECT_EQ(played->getPlayer(0).source(), Source::FaceDown);
  EXPECT_EQ(played->getDrawPile().size(), 2u);
}

// The pile remembers its last play — whose, which cards, and whether it
// burned — until the next play or a pick-up replaces it, so a burn that
// leaves nothing on the pile is still visible.
TEST(Runs, ThePileRemembersItsLastPlayUntilTheNextMove) {
  const GameState g =
      playing({seat("a", {c(Rank::Five), c(Rank::Five, Suit::Hearts), c(Rank::Ten)}),
               seat("b", {c(Rank::Nine), c(Rank::Three)})},
              {c(Rank::Four)});
  EXPECT_EQ(g.getLastPlay(), std::nullopt);
  auto pair = g.playFromHand(0, {0, 1});
  ASSERT_TRUE(pair.ok());
  ASSERT_TRUE(pair->getLastPlay().has_value());
  EXPECT_EQ(*pair->getLastPlay(),
            (LastPlay{"a", {c(Rank::Five), c(Rank::Five, Suit::Hearts)}, false}));
  auto burn = pair->playFromHand(1, {0});  // a nine: no; a ten would burn
  EXPECT_FALSE(burn.ok());
  auto tenBurns = g.playFromHand(0, {2});
  ASSERT_TRUE(tenBurns.ok());
  EXPECT_EQ(*tenBurns->getLastPlay(), (LastPlay{"a", {c(Rank::Ten)}, true}));
  EXPECT_TRUE(tenBurns->getPile().empty());
  // A pick-up clears it: the pile is somebody's hand now.
  const GameState stuck = playing({seat("a", {c(Rank::Three)}), seat("b", {c(Rank::Nine)})},
                                  {c(Rank::King)}, {}, 0, LastPlay{"b", {c(Rank::King)}, false});
  auto pickedUp = stuck.pickUp(0);
  ASSERT_TRUE(pickedUp.ok());
  EXPECT_EQ(pickedUp->getLastPlay(), std::nullopt);
  // A leave keeps it: the pile did not move.
  const GameState three =
      playing({seat("a", {c(Rank::Three)}), seat("b", {c(Rank::Nine)}), seat("c", {c(Rank::Nine)})},
              {c(Rank::King)}, {}, 0, LastPlay{"c", {c(Rank::King)}, false});
  auto left = three.removePlayer(1);
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(left->getLastPlay(), three.getLastPlay());
}

TEST(Ending, TheFirstSeatOutEndsTheGame) {
  const GameState g = playing({seat("a", {c(Rank::Ace)}), seat("b", {c(Rank::Two)}),
                               seat("c", {c(Rank::Four), c(Rank::Four)})});
  auto aOut = g.playFromHand(0, {0});
  ASSERT_TRUE(aOut.ok());
  EXPECT_EQ(aOut->getFinished(), (vector<string>{"a"}));
  EXPECT_EQ(aOut->getPhase(), Phase::Over);
  EXPECT_TRUE(aOut->isOver());
  EXPECT_EQ(aOut->loser(), std::nullopt);
  EXPECT_EQ(aOut->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_FALSE(aOut->playFromHand(1, {0}).ok());
  EXPECT_FALSE(aOut->pickUp(2).ok());
  EXPECT_FALSE(aOut->removePlayer(2).ok());
}

TEST(Ending, ABurnThatShedsTheLastCardEndsTheGame) {
  const GameState g =
      playing({seat("a", {c(Rank::Ten)}), seat("b", {c(Rank::Two)})}, {c(Rank::King)});
  auto over = g.playFromHand(0, {0});
  ASSERT_TRUE(over.ok());
  EXPECT_EQ(over->getPhase(), Phase::Over);
  EXPECT_EQ(over->getFinished(), (vector<string>{"a"}));
  EXPECT_EQ(over->loser(), "b");
}

TEST(Ending, TwoPlayersEndWithTheFirstOut) {
  const GameState g = playing({seat("a", {c(Rank::Ace)}), seat("b", {c(Rank::Two)})});
  auto over = g.playFromHand(0, {0});
  ASSERT_TRUE(over.ok());
  EXPECT_EQ(over->getPhase(), Phase::Over);
  EXPECT_EQ(over->loser(), "b");
  EXPECT_EQ(over->getFinished(), (vector<string>{"a"}));
}

TEST(Ending, TheTurnSkipsSeatsThatAreOut) {
  const GameState g = playing(
      {seat("a", {c(Rank::Five), c(Rank::Five)}), seat("b", {}), seat("c", {c(Rank::Nine)})}, {},
      {}, 0);
  auto next = g.playFromHand(0, {0});
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(next->getWhoseTurn(), 2);
  auto around = next->playFromHand(2, {0});
  ASSERT_TRUE(around.ok());
  EXPECT_EQ(around->getPhase(), Phase::Over);  // only a holds cards
  EXPECT_EQ(around->loser(), "a");
}

TEST(Abandonment, ALeavingSeatCompactsIndicesAndPassesItsTurn) {
  const GameState g =
      playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Six)}), seat("c", {c(Rank::Seven)})},
              {}, {}, 1);
  auto bLeft = g.removePlayer(1);
  ASSERT_TRUE(bLeft.ok());
  ASSERT_EQ(bLeft->getPlayers().size(), 2u);
  EXPECT_EQ(bLeft->getPlayer(1).getId(), "c");
  EXPECT_EQ(bLeft->getWhoseTurn(), 1);  // c, at its new index
  EXPECT_EQ(bLeft->getPhase(), Phase::Playing);

  const GameState late =
      playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Six)}), seat("c", {c(Rank::Seven)})},
              {}, {}, 2);
  auto aLeft = late.removePlayer(0);
  ASSERT_TRUE(aLeft.ok());
  EXPECT_EQ(aLeft->getWhoseTurn(), 1);  // still c
  auto wrapped = late.removePlayer(2);
  ASSERT_TRUE(wrapped.ok());
  EXPECT_EQ(wrapped->getWhoseTurn(), 0);  // c's turn wraps to a

  EXPECT_FALSE(g.removePlayer(3).ok());
}

TEST(Abandonment, BelowTwoSeatsTheGameIsAbandonedWithNoLoser) {
  const GameState g = playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Six)})}, {}, {}, 1);
  auto gone = g.removePlayer(1);
  ASSERT_TRUE(gone.ok());
  EXPECT_EQ(gone->getPhase(), Phase::Abandoned);
  EXPECT_TRUE(gone->isOver());
  EXPECT_EQ(gone->loser(), std::nullopt);
  EXPECT_EQ(gone->getWhoseTurn(), GameState::kNoTurn);
  EXPECT_FALSE(gone->playFromHand(0, {0}).ok());
}

TEST(Abandonment, ALeaversTurnSkipsAnOutSeatThenCompacts) {
  const GameState g = playing({seat("a", {c(Rank::Five)}), seat("b", {}), seat("c", {c(Rank::Six)}),
                               seat("d", {c(Rank::Seven)})},
                              {}, {}, 0);
  auto aLeft = g.removePlayer(0);
  ASSERT_TRUE(aLeft.ok());
  EXPECT_EQ(aLeft->getPlayer(aLeft->getWhoseTurn()).getId(), "c");
  EXPECT_EQ(aLeft->getWhoseTurn(), 1);
}

TEST(Abandonment, ASeatLeavingATwoSeatSetupAbandonsTheGame) {
  const Player a{"a", {c(Rank::Five)}, {}, {}, false};
  const Player b{"b", {c(Rank::Three)}, {}, {}, true};
  GameState setup{{}, {}, {a, b}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto gone = setup.removePlayer(0);
  ASSERT_TRUE(gone.ok());
  EXPECT_EQ(gone->getPhase(), Phase::Abandoned);
  EXPECT_FALSE(gone->ready(0).ok());
}

TEST(Abandonment, TheLastUnreadySeatLeavingOpensPlay) {
  const Player a{"a", {c(Rank::Five)}, {}, {}, true};
  const Player b{"b", {c(Rank::Three)}, {}, {}, true};
  const Player slow{"c", {c(Rank::Four)}, {}, {}, false};
  GameState setup{{}, {}, {a, b, slow}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto open = setup.removePlayer(2);
  ASSERT_TRUE(open.ok());
  EXPECT_EQ(open->getPhase(), Phase::Playing);
  EXPECT_EQ(open->getWhoseTurn(), 1);  // b's three opens

  GameState waiting{{}, {}, {a, slow, b}, GameState::kNoTurn, Phase::Setup, {}, "g", "v"};
  auto stillSetup = waiting.removePlayer(2);
  ASSERT_TRUE(stillSetup.ok());
  EXPECT_EQ(stillSetup->getPhase(), Phase::Setup);
  EXPECT_EQ(stillSetup->getWhoseTurn(), GameState::kNoTurn);
}

TEST(Identity, IdAndVersionRideAlongUnchangedByMoves) {
  const GameState g = playing({seat("a", {c(Rank::Five)}), seat("b", {c(Rank::Six)})});
  const GameState stamped = g.withIdAndVersion("g7", "v3");
  EXPECT_EQ(stamped.getGameId(), "g7");
  EXPECT_EQ(stamped.getVersionId(), "v3");
  auto moved = stamped.playFromHand(0, {0});
  ASSERT_TRUE(moved.ok());
  EXPECT_EQ(moved->getGameId(), "g7");
  EXPECT_EQ(moved->getVersionId(), "v3");
}
