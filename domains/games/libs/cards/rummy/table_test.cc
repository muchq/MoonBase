#include "domains/games/libs/cards/rummy/table.h"

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/gin.h"

using namespace cards;
using namespace rummy;
using std::string;
using std::vector;

namespace {

std::deque<Card> deck() { return NoShuffleDealer().DealNewUnshuffledDeck(); }

TableState opened(vector<string> seats = {"a", "b", "c"}) {
  auto table = TableState::open("T1", seats);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

// The table with this deal in play.
TableState withDeal(const TableState& table, GameState deal) {
  return TableState{table.getSeats(),      table.getWins(),     table.getDealer(),
                    table.getDealNumber(), TablePhase::Playing, Variant::SevenCard,
                    std::move(deal),       table.getGameId(),   table.getVersionId()};
}

}  // namespace

TEST(Variants, NamesRoundTripAndFitTheSeats) {
  for (const Variant variant : {Variant::SevenCard, Variant::TenCard, Variant::Gin}) {
    EXPECT_EQ(parseVariant(variantName(variant)), variant);
  }
  EXPECT_EQ(variantName(Variant::SevenCard), "7-card");
  EXPECT_EQ(variantName(Variant::TenCard), "10-card");
  EXPECT_EQ(variantName(Variant::Gin), "gin");
  EXPECT_FALSE(parseVariant("rummy500").has_value());
  EXPECT_FALSE(parseVariant("basic").has_value());
  EXPECT_FALSE(parseVariant("").has_value());
  // Ten cards a seat leaves too thin a stock at four; gin is for two.
  EXPECT_EQ(variantsFor(2), (vector<Variant>{Variant::SevenCard, Variant::TenCard, Variant::Gin}));
  EXPECT_EQ(variantsFor(3), (vector<Variant>{Variant::SevenCard, Variant::TenCard}));
  EXPECT_EQ(variantsFor(4), vector<Variant>{Variant::SevenCard});
  EXPECT_TRUE(variantsFor(1).empty());
  EXPECT_TRUE(variantsFor(5).empty());
  // Stats record rummy, at either hand size, as the game it always
  // was; gin is a game of its own.
  EXPECT_EQ(recordedName(Variant::SevenCard), "rummy");
  EXPECT_EQ(recordedName(Variant::TenCard), "rummy");
  EXPECT_EQ(recordedName(Variant::Gin), "gin");
}

TEST(Variants, EachDealsItsOwnGame) {
  auto ten = opened().chooseVariant(0, Variant::TenCard, deck());
  ASSERT_TRUE(ten.ok()) << ten.status();
  EXPECT_EQ(ten->getVariant(), Variant::TenCard);
  ASSERT_NE(ten->basicDeal(), nullptr);
  for (const Player& seat : ten->basicDeal()->getPlayers()) EXPECT_EQ(seat.hand.size(), 10u);

  auto gin = opened({"a", "b"}).chooseVariant(0, Variant::Gin, deck());
  ASSERT_TRUE(gin.ok()) << gin.status();
  EXPECT_EQ(gin->basicDeal(), nullptr);
  ASSERT_NE(gin->ginDeal(), nullptr);
  EXPECT_EQ(gin->ginDeal()->getStage(), GinStage::Upcard);
  // The seat after the dealer has the upcard first.
  EXPECT_EQ(gin->ginDeal()->getWhoseTurn(), 1);
  EXPECT_EQ(dealWhoseTurn(*gin->getDeal()), 1);
  EXPECT_EQ(dealPlayers(*gin->getDeal()).at(0).hand.size(), 10u);

  const absl::Status unfit = opened().chooseVariant(0, Variant::Gin, deck()).status();
  EXPECT_EQ(unfit.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(unfit.message(), "that game does not fit this table");
}

TEST(Variants, AMoveOfTheWrongGameIsRefused) {
  auto gin = opened({"a", "b"}).chooseVariant(0, Variant::Gin, deck());
  ASSERT_TRUE(gin.ok());
  const absl::Status refused =
      gin->inDeal<GameState>([](const GameState& deal) { return deal.drawStock(1); }).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "not a move in gin");
  auto passed = gin->inDeal<GinState>([](const GinState& deal) { return deal.pass(1); });
  ASSERT_TRUE(passed.ok()) << passed.status();
  EXPECT_EQ(passed->ginDeal()->getWhoseTurn(), 0);
}

// A gin deal ends like any other: its winner takes the hand, the deal
// passes on. A drawn one credits nobody and still passes on.
TEST(Variants, AGinDealsEndPassesTheDealOn) {
  const Card nine{Suit::Diamonds, Rank::Nine};
  const vector<Card> knocker = {Card{Suit::Hearts, Rank::Three},
                                Card{Suit::Hearts, Rank::Four},
                                Card{Suit::Hearts, Rank::Five},
                                Card{Suit::Clubs, Rank::Seven},
                                Card{Suit::Diamonds, Rank::Seven},
                                Card{Suit::Spades, Rank::Seven},
                                Card{Suit::Spades, Rank::Jack},
                                Card{Suit::Spades, Rank::Queen},
                                Card{Suit::Spades, Rank::King},
                                Card{Suit::Diamonds, Rank::Two},
                                nine};
  std::deque<Card> stock;
  // Two left, as after her draw: her discard draws the deal.
  for (int i = 0; i < 2; i++) stock.emplace_back(i);
  const GinState deal{stock,
                      {},
                      {{"a", knocker}, {"b", {Card{Suit::Clubs, Rank::King}}}},
                      0,
                      GinStage::Play,
                      Phase::Playing,
                      0,
                      std::nullopt,
                      std::nullopt,
                      std::nullopt,
                      "T1",
                      ""};
  const TableState table{{"a", "b"},   {0, 0}, 1,    4, TablePhase::Playing,
                         Variant::Gin, deal,   "T1", ""};

  auto knocked = table.inDeal<GinState>([&](const GinState& d) { return d.knock(0, nine); });
  ASSERT_TRUE(knocked.ok()) << knocked.status();
  EXPECT_EQ(knocked->getPhase(), TablePhase::Choosing);
  EXPECT_EQ(knocked->getWins(), (vector<int>{1, 0}));
  EXPECT_EQ(knocked->getDealer(), 0);
  EXPECT_EQ(dealWinner(*knocked->getDeal()), "a");

  auto drawn = table.inDeal<GinState>([&](const GinState& d) { return d.discard(0, nine); });
  ASSERT_TRUE(drawn.ok()) << drawn.status();
  EXPECT_EQ(drawn->getPhase(), TablePhase::Choosing);
  EXPECT_EQ(drawn->getWins(), (vector<int>{0, 0}));
  EXPECT_EQ(drawn->getDealer(), 0);
  EXPECT_FALSE(dealWinner(*drawn->getDeal()).has_value());

  auto left = table.removePlayer(1);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_TRUE(left->isOver());
  EXPECT_EQ(dealPhase(*left->getDeal()), Phase::Abandoned);
}

TEST(Table, OpensChoosingWithTheFirstSeatDealingAndNoDeal) {
  const TableState table = opened();
  EXPECT_EQ(table.getPhase(), TablePhase::Choosing);
  EXPECT_EQ(table.getDealer(), 0);
  EXPECT_EQ(table.getDealNumber(), 0);
  EXPECT_FALSE(table.getDeal().has_value());
  EXPECT_EQ(table.getWins(), (vector<int>{0, 0, 0}));
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.getGameId(), "T1");
}

TEST(Table, OpensForTwoToFourSeats) {
  EXPECT_FALSE(TableState::open("T", {"a"}).ok());
  EXPECT_FALSE(TableState::open("T", {"a", "b", "c", "d", "e"}).ok());
}

TEST(Table, TheDealerChoosesAndTheSeatAfterOpens) {
  auto dealt = opened().chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  EXPECT_EQ(dealt->getPhase(), TablePhase::Playing);
  EXPECT_EQ(dealt->getDealNumber(), 1);
  EXPECT_EQ(dealt->getVariant(), Variant::SevenCard);
  ASSERT_TRUE(dealt->getDeal().has_value());
  EXPECT_EQ(dealt->basicDeal()->getWhoseTurn(), 1);
  EXPECT_EQ(dealt->basicDeal()->getStage(), Stage::Draw);
  EXPECT_EQ(dealt->basicDeal()->getPlayer(0).hand.size(), 7u);
  EXPECT_EQ(dealt->basicDeal()->getGameId(), "T1");
}

TEST(Table, OnlyTheDealerChoosesUnlessTheDealerIsAway) {
  const TableState table = opened();
  EXPECT_EQ(table.chooseVariant(1, Variant::SevenCard, deck()).status().code(),
            absl::StatusCode::kFailedPrecondition);
  auto away = table.chooseVariant(2, Variant::SevenCard, deck(), /*dealerAway=*/true);
  ASSERT_TRUE(away.ok()) << away.status();
  // The dealer stays the dealer: the seat after them still opens.
  EXPECT_EQ(away->getDealer(), 0);
  EXPECT_EQ(away->basicDeal()->getWhoseTurn(), 1);
  EXPECT_EQ(table.chooseVariant(7, Variant::SevenCard, deck(), true).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(Table, NoChoosingWhileADealIsInPlay) {
  auto dealt = opened().chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  const absl::Status refused = dealt->chooseVariant(0, Variant::SevenCard, deck()).status();
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(refused.message(), "not between deals");
}

TEST(Table, DealMovesGoToTheDealAndNowhereElse) {
  const TableState table = opened();
  EXPECT_EQ(
      table.inDeal<GameState>([](const GameState& d) { return d.drawStock(1); }).status().code(),
      absl::StatusCode::kFailedPrecondition);
  auto dealt = table.chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  auto drew = dealt->inDeal<GameState>([](const GameState& d) { return d.drawStock(1); });
  ASSERT_TRUE(drew.ok()) << drew.status();
  EXPECT_EQ(drew->basicDeal()->getStage(), Stage::Play);
  EXPECT_EQ(drew->getPhase(), TablePhase::Playing);
  // The deal's refusal is the table's.
  EXPECT_EQ(
      dealt->inDeal<GameState>([](const GameState& d) { return d.drawStock(0); }).status().code(),
      absl::StatusCode::kFailedPrecondition);
}

TEST(Table, ADealWonByPlayScoresTheHandAndTheDealPassesOn) {
  const TableState table = opened();
  // b holds one card after drawing; discarding it goes out.
  GameState deal{{Card{Suit::Clubs, Rank::Two}},
                 {Card{Suit::Clubs, Rank::Three}},
                 {{"a", {Card{Suit::Clubs, Rank::Four}}},
                  {"b", {Card{Suit::Clubs, Rank::Five}}},
                  {"c", {Card{Suit::Clubs, Rank::Six}}}},
                 {},
                 1,
                 Stage::Play,
                 Phase::Playing,
                 std::nullopt,
                 "T1",
                 ""};
  auto out = withDeal(table, deal).inDeal<GameState>([](const GameState& d) {
    return d.discard(1, Card{Suit::Clubs, Rank::Five});
  });
  ASSERT_TRUE(out.ok()) << out.status();
  EXPECT_EQ(out->getPhase(), TablePhase::Choosing);
  EXPECT_EQ(out->getWins(), (vector<int>{0, 1, 0}));
  EXPECT_EQ(out->getDealer(), 1);
  // The finished deal stays, to show its hands and result.
  ASSERT_TRUE(out->getDeal().has_value());
  EXPECT_EQ(out->basicDeal()->winner(), "b");
  EXPECT_FALSE(out->isOver());
  // The next dealer is b; the seat after b opens.
  auto next = out->chooseVariant(1, Variant::SevenCard, deck());
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->getDealNumber(), out->getDealNumber() + 1);
  EXPECT_EQ(next->basicDeal()->getWhoseTurn(), 2);
  EXPECT_EQ(next->getWins(), (vector<int>{0, 1, 0}));
}

TEST(Table, TheDealWrapsRoundTheTable) {
  const TableState last{{"a", "b"},         {0, 0},       1,   3, TablePhase::Choosing,
                        Variant::SevenCard, std::nullopt, "T", ""};
  auto dealt = last.chooseVariant(1, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  EXPECT_EQ(dealt->basicDeal()->getWhoseTurn(), 0);
}

TEST(Table, ALeaveWhileChoosingCompactsTheSeatsAndTheDealerFollows) {
  const TableState choosing{{"a", "b", "c"},    {2, 1, 0},    2,   3, TablePhase::Choosing,
                            Variant::SevenCard, std::nullopt, "T", ""};
  auto left = choosing.removePlayer(0);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_EQ(left->getSeats(), (vector<string>{"b", "c"}));
  EXPECT_EQ(left->getWins(), (vector<int>{1, 0}));
  EXPECT_EQ(left->getSeats().at(left->getDealer()), "c");
  EXPECT_EQ(left->getPhase(), TablePhase::Choosing);
}

TEST(Table, TheDealerLeavingPassesTheChoiceOn) {
  const TableState choosing{{"a", "b", "c"},    {0, 0, 0},    2,   1, TablePhase::Choosing,
                            Variant::SevenCard, std::nullopt, "T", ""};
  auto left = choosing.removePlayer(2);
  ASSERT_TRUE(left.ok());
  // The seat after the leaver, wrapping.
  EXPECT_EQ(left->getSeats().at(left->getDealer()), "a");
}

TEST(Table, ADealerMidTableLeavingPassesTheChoiceToTheSeatAfter) {
  const TableState choosing{{"a", "b", "c", "d"}, {0, 0, 0, 0}, 1,   1, TablePhase::Choosing,
                            Variant::SevenCard,   std::nullopt, "T", ""};
  auto left = choosing.removePlayer(1);
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(left->getSeats().at(left->getDealer()), "c");
}

TEST(Table, ALeaveMidDealLeavesTheDealToo) {
  auto dealt = opened().chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  auto left = dealt->removePlayer(2);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_EQ(left->getPhase(), TablePhase::Playing);
  EXPECT_EQ(left->getSeats(), (vector<string>{"a", "b"}));
  EXPECT_EQ(left->basicDeal()->getPlayers().size(), 2u);
  EXPECT_EQ(left->getWins().size(), 2u);
}

// Mid-deal the dealer is the seat that dealt the deal in play; its end
// passes the deal to the seat after. A dealer who leaves mid-deal still
// passes it to the seat after them, not the one after that.
TEST(Table, TheDealerLeavingMidDealPassesTheNextDealToTheSeatAfter) {
  // a dealt; b is on turn holding one card after drawing.
  GameState deal{{Card{Suit::Clubs, Rank::Two}},
                 {Card{Suit::Clubs, Rank::Three}},
                 {{"a", {Card{Suit::Clubs, Rank::Four}}},
                  {"b", {Card{Suit::Clubs, Rank::Five}}},
                  {"c", {Card{Suit::Clubs, Rank::Six}}}},
                 {},
                 1,
                 Stage::Play,
                 Phase::Playing,
                 std::nullopt,
                 "T1",
                 ""};
  auto left = withDeal(opened(), deal).removePlayer(0);
  ASSERT_TRUE(left.ok()) << left.status();
  ASSERT_EQ(left->getPhase(), TablePhase::Playing);
  auto out = left->inDeal<GameState>(
      [](const GameState& d) { return d.discard(0, Card{Suit::Clubs, Rank::Five}); });
  ASSERT_TRUE(out.ok()) << out.status();
  ASSERT_EQ(out->getPhase(), TablePhase::Choosing);
  EXPECT_EQ(out->getSeats().at(out->getDealer()), "b");

  // The last seat dealing and leaving: the deal passes round to the first.
  const TableState last_dealt{{"a", "b", "c"},
                              {0, 0, 0},
                              2,
                              1,
                              TablePhase::Playing,
                              Variant::SevenCard,
                              GameState{{Card{Suit::Clubs, Rank::Two}},
                                        {Card{Suit::Clubs, Rank::Three}},
                                        {{"a", {Card{Suit::Clubs, Rank::Four}}},
                                         {"b", {Card{Suit::Clubs, Rank::Five}}},
                                         {"c", {Card{Suit::Clubs, Rank::Six}}}},
                                        {},
                                        0,
                                        Stage::Play,
                                        Phase::Playing,
                                        std::nullopt,
                                        "T1",
                                        ""},
                              "T1",
                              ""};
  auto gone = last_dealt.removePlayer(2);
  ASSERT_TRUE(gone.ok()) << gone.status();
  auto ended = gone->inDeal<GameState>(
      [](const GameState& d) { return d.discard(0, Card{Suit::Clubs, Rank::Four}); });
  ASSERT_TRUE(ended.ok()) << ended.status();
  EXPECT_EQ(ended->getSeats().at(ended->getDealer()), "a");
}

TEST(Table, BelowTwoSeatsTheTableClosesWithAnyDealInPlay) {
  auto dealt = opened({"a", "b"}).chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  auto left = dealt->removePlayer(0);
  ASSERT_TRUE(left.ok());
  EXPECT_TRUE(left->isOver());
  EXPECT_EQ(left->basicDeal()->getPhase(), Phase::Abandoned);
  auto choosing_left = opened({"a", "b"}).removePlayer(1);
  ASSERT_TRUE(choosing_left.ok());
  EXPECT_TRUE(choosing_left->isOver());
  // Closed is closed.
  EXPECT_FALSE(left->removePlayer(0).ok());
  EXPECT_FALSE(left->chooseVariant(0, Variant::SevenCard, deck()).ok());
}

TEST(Table, SeatsAreFoundByIdAndTheIdsAreTheRowsToSet) {
  const TableState table = opened();
  EXPECT_EQ(table.playerIndex("c"), 2);
  EXPECT_EQ(table.playerIndex("z"), -1);
  auto dealt = table.chooseVariant(0, Variant::SevenCard, deck());
  ASSERT_TRUE(dealt.ok());
  const TableState stamped = dealt->withIdAndVersion("G", "V");
  EXPECT_EQ(stamped.getGameId(), "G");
  EXPECT_EQ(stamped.getVersionId(), "V");
  EXPECT_EQ(stamped.basicDeal()->getGameId(), "G");
}

TEST(Deal, OpensAtTheSeatNamed) {
  NoShuffleDealer dealer;
  auto game = dealRummyGame("g", {"a", "b", "c"}, dealer.DealNewUnshuffledDeck(), 2);
  ASSERT_TRUE(game.ok());
  EXPECT_EQ(game->getWhoseTurn(), 2);
  EXPECT_FALSE(dealRummyGame("g", {"a", "b"}, dealer.DealNewUnshuffledDeck(), 2).ok());
}
