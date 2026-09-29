#include "domains/games/libs/cards/rummy/table_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/game_state_serde.h"
#include "domains/games/libs/cards/rummy/gin_serde.h"
#include "domains/games/libs/cards/rummy/table.h"

using namespace cards;
using namespace rummy;
using nlohmann::json;

namespace {

std::deque<Card> pristineDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

TableState opened() {
  auto table = TableState::open("T", {"a", "b"});
  EXPECT_TRUE(table.ok());
  return *table;
}

TableState playing() {
  auto dealt = opened().chooseVariant(0, Variant::SevenCard, pristineDeck());
  EXPECT_TRUE(dealt.ok()) << dealt.status();
  return *dealt;
}

// Between deals: the last deal over by play, b having gone out.
TableState between() {
  GameState over{{Card{Suit::Clubs, Rank::Two}},
                 {Card{Suit::Clubs, Rank::Three}},
                 {{"a", {Card{Suit::Clubs, Rank::King}}}, {"b", {}}},
                 {},
                 GameState::kNoTurn,
                 Stage::Draw,
                 Phase::Over,
                 std::nullopt,
                 "",
                 ""};
  return TableState{{"a", "b"},         {0, 1}, 0,  1, TablePhase::Choosing,
                    Variant::SevenCard, over,   "", ""};
}

json payloadOf(const TableState& table) { return json::parse(serializeTableState(table)); }

void expectRejected(const json& payload) {
  const auto restored = deserializeTableState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

void expectRoundTrips(const TableState& table) {
  const std::string serialized = serializeTableState(table);
  const auto restored = deserializeTableState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status() << "\n" << serialized;
  EXPECT_EQ(restored->getSeats(), table.getSeats());
  EXPECT_EQ(restored->getWins(), table.getWins());
  EXPECT_EQ(restored->getDealer(), table.getDealer());
  EXPECT_EQ(restored->getDealNumber(), table.getDealNumber());
  EXPECT_EQ(restored->getPhase(), table.getPhase());
  EXPECT_EQ(restored->getVariant(), table.getVariant());
  ASSERT_EQ(restored->getDeal().has_value(), table.getDeal().has_value());
  if (table.rummyDeal() != nullptr) {
    ASSERT_NE(restored->rummyDeal(), nullptr);
    EXPECT_EQ(serializeGameState(*restored->rummyDeal()), serializeGameState(*table.rummyDeal()));
  }
  if (table.ginDeal() != nullptr) {
    ASSERT_NE(restored->ginDeal(), nullptr);
    EXPECT_EQ(serializeGinState(*restored->ginDeal()), serializeGinState(*table.ginDeal()));
  }
  EXPECT_EQ(serializeTableState(*restored), serialized);
}

}  // namespace

// Each variant's deal is stored in its own engine's form: 10-card as
// GameState's, gin as GinState's.
TEST(TableSerde, EveryVariantsDealRoundTrips) {
  auto ten = opened().chooseVariant(0, Variant::TenCard, pristineDeck());
  ASSERT_TRUE(ten.ok());
  expectRoundTrips(*ten);
  auto gin = opened().chooseVariant(0, Variant::Gin, pristineDeck());
  ASSERT_TRUE(gin.ok()) << gin.status();
  expectRoundTrips(*gin);
  const json payload = payloadOf(*gin);
  EXPECT_EQ(payload["variant"], "gin");
  EXPECT_EQ(payload["deal"]["stage"], "upcard");

  // A deal in another game's form than the table's variant is no table.
  json basicAsGin = payloadOf(playing());
  basicAsGin["variant"] = "gin";
  expectRejected(basicAsGin);
  json ginAsBasic = payload;
  ginAsBasic["variant"] = "basic";
  expectRejected(ginAsBasic);
}

TEST(TableSerde, EveryPhaseRoundTrips) {
  expectRoundTrips(opened());
  expectRoundTrips(playing());
  expectRoundTrips(between());
  auto closed = playing().removePlayer(0);
  ASSERT_TRUE(closed.ok());
  expectRoundTrips(*closed);
}

// The exact bytes of a table opened and not yet dealt. A change to the
// shape is a schema change: a version bump, not an edit here. Seven-card
// is stored as "basic", its name before 10-card and gin (#1610), so a
// hub rolled back past them still reads every seven-card table.
TEST(TableSerde, FrozenPayload) {
  constexpr const char* kRow =
      R"({"dealNumber":0,"dealer":0,"phase":"choosing","seats":["a","b"],"v":2,)"
      R"("variant":"basic","wins":[0,0]})";
  EXPECT_EQ(serializeTableState(opened()), kRow);
  const auto restored = deserializeTableState(kRow);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->getPhase(), TablePhase::Choosing);
  EXPECT_EQ(restored->getVariant(), Variant::SevenCard);
  // The deal nests as the v1 deal it is, so the deal's own schema pins it.
  const json dealt = payloadOf(playing());
  EXPECT_EQ(dealt["phase"], "playing");
  EXPECT_EQ(dealt["deal"]["v"], 1);
  EXPECT_EQ(dealt["dealNumber"], 1);
}

// A row from before the table (#1608) is one deal: it reads as that deal
// in play at a table of its seats, dealt by the seat before the one on
// turn, or as a closed table if the deal had ended — a finished row stays
// finished.
TEST(TableSerde, AVersionOneRowIsOneDeal) {
  auto deal = dealRummyGame("", {"a", "b", "c"}, pristineDeck());
  ASSERT_TRUE(deal.ok());
  auto drew = deal->drawStock(0);
  ASSERT_TRUE(drew.ok());
  auto table = deserializeTableState(serializeGameState(*drew));
  ASSERT_TRUE(table.ok()) << table.status();
  EXPECT_EQ(table->getPhase(), TablePhase::Playing);
  EXPECT_EQ(table->getSeats(), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(table->getWins(), (std::vector<int>{0, 0, 0}));
  EXPECT_EQ(table->getDealNumber(), 1);
  EXPECT_EQ(table->getDealer(), 2);
  EXPECT_EQ(table->getVariant(), Variant::SevenCard);
  EXPECT_EQ(serializeGameState(*table->rummyDeal()), serializeGameState(*drew));

  auto gone = drew->removePlayer(1);
  ASSERT_TRUE(gone.ok());
  auto two = gone->removePlayer(1);
  ASSERT_TRUE(two.ok());
  ASSERT_TRUE(two->isOver());
  auto closed = deserializeTableState(serializeGameState(*two));
  ASSERT_TRUE(closed.ok()) << closed.status();
  EXPECT_TRUE(closed->isOver());
}

TEST(TableSerde, RejectsATableTheEngineCouldNotPlay) {
  json payload = payloadOf(opened());
  payload["v"] = 3;
  expectRejected(payload);

  for (const char* key : {"phase", "seats", "wins", "dealer", "dealNumber", "variant"}) {
    payload = payloadOf(opened());
    payload.erase(key);
    expectRejected(payload);
  }
  payload = payloadOf(opened());
  payload["variant"] = "canasta";
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["phase"] = "dealing";
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["wins"] = json::array({0});  // one short of the seats
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["wins"] = json::array({0, -1});
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["dealer"] = 2;
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["seats"] = json::array({"a"});
  payload["wins"] = json::array({0});
  expectRejected(payload);  // an open table seats two

  // In play: a deal, in play, of exactly the table's seats.
  payload = payloadOf(playing());
  payload.erase("deal");
  expectRejected(payload);
  payload = payloadOf(playing());
  payload["seats"] = json::array({"a", "z"});
  expectRejected(payload);
  payload = payloadOf(playing());
  payload["deal"]["phase"] = "over";
  payload["deal"]["whoseTurn"] = -1;
  expectRejected(payload);
  payload = payloadOf(playing());
  payload["deal"] = 7;
  expectRejected(payload);

  // Between deals: the last deal, ended; none before the first.
  payload = payloadOf(between());
  payload["deal"]["phase"] = "playing";
  payload["deal"]["whoseTurn"] = 0;
  payload["deal"]["players"][0]["hand"] = json::array({3});
  expectRejected(payload);
  payload = payloadOf(opened());
  payload["dealNumber"] = 1;
  expectRejected(payload);  // dealt once, and no deal to show
  payload = payloadOf(between());
  payload["dealNumber"] = 0;
  expectRejected(payload);  // a deal, and none dealt

  for (const char* input : {"", "[]", "not json", R"({"v":2})"}) {
    EXPECT_FALSE(deserializeTableState(input).ok()) << input;
  }
}

// The score sheet rides the row, a line a deal; a row from before the
// sheet reads as a table with none. Old hubs ignore the key, so a
// rollback keeps the table and loses only its sheet.
TEST(TableSerde, TheScoreSheetRoundTrips) {
  const TableState scored{
      {"a", "b"},
      {0, 1},
      0,
      2,
      TablePhase::Choosing,
      Variant::SevenCard,
      between().getDeal(),
      "",
      "",
      {{Variant::SevenCard, std::string("b"), 14}, {Variant::Gin, std::nullopt, 0}}};
  const json row = payloadOf(scored);
  EXPECT_EQ(row["scoreSheet"].dump(),
            R"([{"points":14,"variant":"basic","winner":"b"},{"points":0,"variant":"gin"}])");
  const auto restored = deserializeTableState(row.dump());
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->getScoreSheet(), scored.getScoreSheet());

  json malformed = row;
  malformed["scoreSheet"][0]["points"] = -3;
  expectRejected(malformed);
  malformed = row;
  malformed["scoreSheet"][0]["variant"] = "canasta";
  expectRejected(malformed);
}

TEST(TableSerde, NulInASeatIdIsReplaced) {
  const std::string nul_id("a\0b", 3);
  auto table = TableState::open("T", {nul_id, "b"});
  ASSERT_TRUE(table.ok());
  const std::string serialized = serializeTableState(*table);
  EXPECT_EQ(serialized.find('\0'), std::string::npos);
  EXPECT_TRUE(deserializeTableState(serialized).ok());
}
