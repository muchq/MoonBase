#include "domains/games/libs/cards/rummy/gin_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/rummy/gin.h"

using namespace cards;
using namespace rummy;
using nlohmann::json;

namespace {

GinState dealt() {
  auto deal = dealGin("g", {"a", "b"}, NoShuffleDealer().DealNewUnshuffledDeck(), 1);
  EXPECT_TRUE(deal.ok()) << deal.status();
  return *deal;
}

// Seat 1 passes, seat 0 takes the upcard.
GinState taken() {
  auto passed = dealt().pass(1);
  EXPECT_TRUE(passed.ok());
  auto took = passed->drawDiscard(0);
  EXPECT_TRUE(took.ok()) << took.status();
  return *took;
}

// Seat 1 draws the stock after both pass, and knocks: with the pristine
// deck seat 1 holds A♥ A♣ K♥ K♣ Q♥ Q♣ J♥ J♣ 10♥ 10♣, two runs; 9♥ from
// the stock extends hearts, and throwing it is gin.
GinState ginned() {
  auto one = dealt().pass(1);
  auto two = one->pass(0);
  auto drew = two->drawStock(1);
  EXPECT_TRUE(drew.ok()) << drew.status();
  auto knocked = drew->knock(1, drew->getPlayer(1).hand.back());
  EXPECT_TRUE(knocked.ok()) << knocked.status();
  return *knocked;
}

void expectRoundTrips(const GinState& state) {
  const std::string serialized = serializeGinState(state);
  const auto restored = deserializeGinState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status() << "\n" << serialized;
  EXPECT_EQ(serializeGinState(*restored), serialized);
  EXPECT_EQ(restored->getStage(), state.getStage());
  EXPECT_EQ(restored->getPhase(), state.getPhase());
  EXPECT_EQ(restored->getWhoseTurn(), state.getWhoseTurn());
  EXPECT_EQ(restored->getUpcardPasses(), state.getUpcardPasses());
  EXPECT_EQ(restored->getTakenDiscard(), state.getTakenDiscard());
  EXPECT_EQ(restored->getLastMove(), state.getLastMove());
  EXPECT_EQ(restored->getPlayers(), state.getPlayers());
  EXPECT_EQ(restored->winner(), state.winner());
  EXPECT_EQ(restored->winnerPoints(), state.winnerPoints());
}

void expectRejected(const json& payload) {
  const auto restored = deserializeGinState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace

TEST(GinSerde, EveryStageAndEndingRoundTrips) {
  expectRoundTrips(dealt());
  expectRoundTrips(*dealt().pass(1));
  expectRoundTrips(*dealt().pass(1)->pass(0));
  expectRoundTrips(taken());
  expectRoundTrips(*taken().discard(0, taken().getPlayer(0).hand.front()));
  const GinState gin = ginned();
  ASSERT_EQ(gin.getResult()->ending, GinEnding::Gin);
  expectRoundTrips(gin);
  const auto restored = deserializeGinState(serializeGinState(gin));
  ASSERT_TRUE(restored.ok());
  EXPECT_EQ(restored->getResult()->hands.at(1).melds, gin.getResult()->hands.at(1).melds);
  EXPECT_EQ(restored->getResult()->hands.at(0).deadwood, gin.getResult()->hands.at(0).deadwood);
  expectRoundTrips(*taken().removePlayer(1));
}

// The bytes of a fresh deal. A change to the shape is a schema change: a
// version bump, not an edit here.
TEST(GinSerde, FrozenPayload) {
  constexpr const char* kRow =
      R"({"discard":[31],"phase":"playing","players":[{"hand":[51,49,47,45,43,41,39,37,35,33],)"
      R"("id":"a"},{"hand":[50,48,46,44,42,40,38,36,34,32],"id":"b"}],"stage":"upcard",)"
      R"("stock":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,)"
      R"(29,30],"upcardPasses":0,"v":1,"whoseTurn":1})";
  EXPECT_EQ(serializeGinState(dealt()), kRow);
  ASSERT_TRUE(deserializeGinState(kRow).ok());
}

TEST(GinSerde, RejectsWhatTheEngineCouldNotPlay) {
  const json base = json::parse(serializeGinState(taken()));
  json payload = base;
  payload["v"] = 2;
  expectRejected(payload);
  for (const char* key :
       {"stock", "discard", "players", "whoseTurn", "stage", "phase", "upcardPasses"}) {
    payload = base;
    payload.erase(key);
    expectRejected(payload);
  }
  payload = base;
  payload["stage"] = "melding";
  expectRejected(payload);
  payload = base;
  payload["whoseTurn"] = 2;
  expectRejected(payload);
  payload = base;
  payload["players"].erase(1);
  expectRejected(payload);  // playing with one seat
  payload = base;
  payload["upcardPasses"] = 2;
  expectRejected(payload);
  payload = base;
  payload["stock"][0] = 52;
  expectRejected(payload);
  payload = base;
  payload["lastMove"]["kind"] = "meld";
  expectRejected(payload);
  // No hand holds more than ten and the drawn card; the search that
  // arranges a hand is sized for that.
  payload = base;
  for (int card = 0; card < 12; card++) payload["players"][1]["hand"].push_back(card);
  expectRejected(payload);
  // A playing row has a move to make: something to take or draw at the
  // upcard or the draw, a card to throw mid-turn.
  const json upcard = json::parse(serializeGinState(dealt()));
  payload = upcard;
  payload["discard"] = json::array();
  expectRejected(payload);
  const json stockOnly = json::parse(serializeGinState(*dealt().pass(1)->pass(0)));
  payload = stockOnly;
  payload["stock"] = json::array();
  expectRejected(payload);
  payload = base;
  payload["players"][0]["hand"] = json::array();
  expectRejected(payload);

  const json over = json::parse(serializeGinState(ginned()));
  payload = over;
  payload["result"]["winner"] = 5;
  expectRejected(payload);
  payload = over;
  payload["result"]["ending"] = "schneider";
  expectRejected(payload);
  payload = over;
  payload["result"]["hands"].erase(0);
  expectRejected(payload);
  payload = over;
  payload.erase("result");
  expectRejected(payload);  // over by play, and no result

  for (const char* input : {"", "[]", "not json", R"({"v":1})"}) {
    EXPECT_FALSE(deserializeGinState(input).ok()) << input;
  }
}
