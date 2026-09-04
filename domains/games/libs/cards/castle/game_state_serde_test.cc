#include "domains/games/libs/cards/castle/game_state_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/castle/player.h"

using namespace cards;
using namespace castle;
using nlohmann::json;

namespace {

// Card(i) inverts intValue(), so 0..51 is the full deck in a fixed order,
// dealt from the back: the same determinism the hub's NoShuffleDealer
// relies on.
std::deque<Card> pristineDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

GameState dealt() {
  const auto state = dealCastleGame("g", {"a", "b"}, pristineDeck());
  EXPECT_TRUE(state.ok()) << state.status();
  return *state;
}

// A known-good serialized game as an editable payload; negative tests
// corrupt exactly the field they name and nothing else.
json dealtPayload() { return json::parse(serializeGameState(dealt())); }

void expectRejected(const json& payload) {
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

// Engine truth only: identity is the storage row's business, so the
// serde returns it empty and the comparison excludes it.
void expectStatesEqual(const GameState& a, const GameState& b) {
  EXPECT_EQ(a.getDrawPile(), b.getDrawPile());
  EXPECT_EQ(a.getPile(), b.getPile());
  EXPECT_EQ(a.getPlayers(), b.getPlayers());
  EXPECT_EQ(a.getWhoseTurn(), b.getWhoseTurn());
  EXPECT_EQ(a.getPhase(), b.getPhase());
  EXPECT_EQ(a.getFinished(), b.getFinished());
}

// Serialize -> deserialize -> compare, and serialize again: the second
// pass must be byte-identical, so stored states re-serialize stably.
void expectRoundTrips(const GameState& state) {
  const std::string serialized = serializeGameState(state);
  const auto restored = deserializeGameState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(state, *restored);
  EXPECT_EQ(restored->getGameId(), "");
  EXPECT_EQ(restored->getVersionId(), "");
  EXPECT_EQ(serializeGameState(*restored), serialized);
}

TEST(CastleSerde, ADealtGameRoundTrips) { expectRoundTrips(dealt()); }

// Every phase the engine can be in, reached by play: setup with a swap
// and one ready, playing with a card on the pile and a hand redrawn,
// and both endings.
TEST(CastleSerde, EveryPhaseRoundTrips) {
  auto state = dealt();
  auto swapped = state.swapForSetup(0, 0, 2);
  ASSERT_TRUE(swapped.ok()) << swapped.status();
  auto one_ready = swapped->ready(0);
  ASSERT_TRUE(one_ready.ok());
  expectRoundTrips(*one_ready);

  auto playing = one_ready->ready(1);
  ASSERT_TRUE(playing.ok());
  ASSERT_EQ(playing->getPhase(), Phase::Playing);
  const int opener = playing->getWhoseTurn();
  auto played = playing->playFromHand(opener, {0});
  ASSERT_TRUE(played.ok()) << played.status();
  EXPECT_EQ(played->getPile().size(), 1u);
  expectRoundTrips(*played);

  auto abandoned = played->removePlayer(opener);
  ASSERT_TRUE(abandoned.ok());
  ASSERT_EQ(abandoned->getPhase(), Phase::Abandoned);
  expectRoundTrips(*abandoned);

  // A hand-built finish: one seat out, the other holding the loser's cards.
  const GameState over{{},
                       {Card(3)},
                       {Player{"a", {}, {}, {}, true}, Player{"b", {Card(5)}, {}, {}, true}},
                       GameState::kNoTurn,
                       Phase::Over,
                       {"a"},
                       "g",
                       "1"};
  expectRoundTrips(over);
}

// The exact bytes a fresh two-seat deal stores. A change here is a
// schema change and means a version bump, not an edit to this literal.
TEST(CastleSerde, FrozenPayload) {
  constexpr const char* kRow =
      R"({"drawPile":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33],)"
      R"("finished":[],"phase":"setup","pile":[],)"
      R"("players":[{"faceDown":[51,50,49],"faceUp":[48,47,46],"hand":[45,44,43],"id":"a","ready":false},)"
      R"({"faceDown":[42,41,40],"faceUp":[39,38,37],"hand":[36,35,34],"id":"b","ready":false}],)"
      R"("v":1,"whoseTurn":-1})";
  EXPECT_EQ(serializeGameState(dealt()), kRow);
  const auto restored = deserializeGameState(kRow);
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(dealt(), *restored);
}

// The bytes of a playing row: the other spelling a rename of a phase
// name would orphan. Setup's spelling is pinned above; over/abandoned
// ride the same table of names.
TEST(CastleSerde, FrozenPlayingPayload) {
  constexpr const char* kRow =
      R"({"drawPile":[0,1,2,3],"finished":[],"phase":"playing","pile":[45,44],)"
      R"("players":[{"faceDown":[51],"faceUp":[48,47],"hand":[43],"id":"a","ready":true},)"
      R"({"faceDown":[42,41,40],"faceUp":[39],"hand":[36,35,34],"id":"b","ready":true}],)"
      R"("v":1,"whoseTurn":1})";
  const auto restored = deserializeGameState(kRow);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->getPhase(), Phase::Playing);
  EXPECT_EQ(restored->getWhoseTurn(), 1);
  EXPECT_EQ(serializeGameState(*restored), kRow);
  for (const char* phase : {"over", "abandoned"}) {
    json payload = json::parse(kRow);
    payload["phase"] = phase;
    payload["whoseTurn"] = -1;
    const auto ended = deserializeGameState(payload.dump());
    ASSERT_TRUE(ended.ok()) << phase;
    EXPECT_TRUE(ended->isOver());
  }
}

TEST(CastleSerde, RejectsWhatTheEngineWouldIndexOutOfRange) {
  json payload = dealtPayload();
  payload["v"] = 2;
  expectRejected(payload);

  payload = dealtPayload();
  payload["whoseTurn"] = 2;  // two seats: -1..1
  expectRejected(payload);
  payload["whoseTurn"] = -2;
  expectRejected(payload);
  // No turn is setup's and the end's; a playing row must name a seat.
  payload = dealtPayload();
  payload["phase"] = "playing";
  payload["whoseTurn"] = -1;
  expectRejected(payload);

  // Integers read as int64 before narrowing: 2^32+1 must not wrap to 1.
  payload = dealtPayload();
  payload["whoseTurn"] = 4294967297;
  expectRejected(payload);
  payload = dealtPayload();
  payload["players"][0]["hand"][0] = 4294967297;
  expectRejected(payload);

  // Every player field is required with its type.
  for (const char* key : {"id", "hand", "faceUp", "faceDown", "ready"}) {
    payload = dealtPayload();
    payload["players"][0].erase(key);
    expectRejected(payload);
    payload = dealtPayload();
    payload["players"][0][key] = 7;
    expectRejected(payload);
  }

  payload = dealtPayload();
  payload["players"][0]["faceUp"].push_back(7);  // four on a three-card row
  expectRejected(payload);

  payload = dealtPayload();
  payload["players"][1]["faceDown"][0] = 52;
  expectRejected(payload);

  payload = dealtPayload();
  payload["phase"] = "dealing";
  expectRejected(payload);

  payload = dealtPayload();
  payload["players"] = json::array();
  expectRejected(payload);

  payload = dealtPayload();
  payload["finished"] = json::array({1});
  expectRejected(payload);

  for (const char* input : {"", "[]", "not json", R"({"v":1})", R"({"v":"1"})"}) {
    const auto restored = deserializeGameState(input);
    EXPECT_FALSE(restored.ok()) << input;
  }
}

TEST(CastleSerde, UnknownFieldsAreIgnored) {
  json payload = dealtPayload();
  payload["future"] = "field";
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(dealt(), *restored);
}

// A NUL in a player id would fail the postgres write; it is replaced,
// not fatal.
TEST(CastleSerde, NulInAPlayerIdIsReplaced) {
  const std::string nul_id("a\0b", 3);
  const auto state = dealCastleGame("g", {nul_id, "b"}, pristineDeck());
  ASSERT_TRUE(state.ok());
  const std::string serialized = serializeGameState(*state);
  EXPECT_EQ(serialized.find('\0'), std::string::npos);
  EXPECT_NE(serialized.find("a\xEF\xBF\xBD"
                            "b"),
            std::string::npos);
  ASSERT_TRUE(deserializeGameState(serialized).ok());
}

}  // namespace
