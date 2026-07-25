#include "domains/games/libs/cards/golf/game_state_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/player.h"

using namespace cards;
using namespace golf;
using nlohmann::json;

namespace {

// Card(i) inverts intValue(), so 0..51 is the full deck in a fixed order —
// the same determinism trick the hub's NoShuffleDealer relies on.
std::deque<Card> pristineDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

// A known-good serialized game as an editable payload; negative tests
// corrupt exactly the field they name and nothing else.
json dealtPayload() {
  const auto dealt = dealGolfGame("g", {"a", "b"}, pristineDeck());
  EXPECT_TRUE(dealt.ok()) << dealt.status();
  return json::parse(serializeGameState(*dealt));
}

void expectRejected(const json& payload) {
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

// Engine truth only: identity is the storage row's business, so the
// serde returns it empty and the comparison excludes it.
void expectStatesEqual(const GameState& a, const GameState& b) {
  EXPECT_EQ(a.getDrawPile(), b.getDrawPile());
  EXPECT_EQ(a.getDiscardPile(), b.getDiscardPile());
  EXPECT_EQ(a.getPlayers(), b.getPlayers());
  EXPECT_EQ(a.getPeekedAtDrawPile(), b.getPeekedAtDrawPile());
  EXPECT_EQ(a.getWhoseTurn(), b.getWhoseTurn());
  EXPECT_EQ(a.getWhoKnocked(), b.getWhoKnocked());
  EXPECT_EQ(a.getPeeksHidden(), b.getPeeksHidden());
}

// Serialize -> deserialize -> compare, and serialize again: the second
// pass must be byte-identical, so stored states re-serialize stably.
void expectRoundTrips(const GameState& state) {
  const std::string serialized = serializeGameState(state);
  const auto restored = deserializeGameState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(state, *restored);
  EXPECT_TRUE(restored->getGameId().empty());
  EXPECT_TRUE(restored->getVersionId().empty());
  EXPECT_EQ(serializeGameState(*restored), serialized);
}

TEST(GameStateSerde, RoundTripsAFreshDeal) {
  const auto dealt = dealGolfGame("g-fresh", {"alice", "bob", "cara"}, pristineDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  expectRoundTrips(*dealt);
}

TEST(GameStateSerde, RoundTripsEveryStateOfADeterministicGame) {
  // GameState is immutable (const members, no assignment), so the walk
  // re-emplaces into an optional at every transition.
  std::optional<GameState> state;
  auto dealt = dealGolfGame("g-full", {"alice", "bob"}, pristineDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  state.emplace(*std::move(dealt));
  expectRoundTrips(*state);

  const auto advance = [&state](absl::StatusOr<GameState> next) {
    ASSERT_TRUE(next.ok()) << next.status();
    state.emplace(*std::move(next));
    expectRoundTrips(*state);
  };

  // The opening reveal: both players peek two of their own cards, then
  // one hide ends the countdown for the table.
  for (const int player : {0, 1}) {
    for (const Position position : {Position::TopLeft, Position::BottomRight}) {
      ASSERT_NO_FATAL_FAILURE(advance(state->peekOwnCard(player, position)));
    }
  }
  ASSERT_NO_FATAL_FAILURE(advance(state->hideCards(0)));

  // Turn play, every mechanic once. Draw-pile moves are draw-then-decide:
  // peekAtDrawPile is the draw, then the swap either keeps or declines it.
  ASSERT_NO_FATAL_FAILURE(advance(state->peekAtDrawPile(0)));
  ASSERT_NO_FATAL_FAILURE(advance(state->swapForDrawPile(0, Position::TopLeft)));
  ASSERT_NO_FATAL_FAILURE(advance(state->swapForDiscardPile(1, Position::BottomLeft)));
  ASSERT_NO_FATAL_FAILURE(advance(state->peekAtDrawPile(0)));
  ASSERT_NO_FATAL_FAILURE(advance(state->swapDrawForDiscardPile(0)));
  ASSERT_NO_FATAL_FAILURE(advance(state->knock(1)));
  ASSERT_NO_FATAL_FAILURE(advance(state->peekAtDrawPile(0)));
  ASSERT_NO_FATAL_FAILURE(advance(state->swapForDrawPile(0, Position::TopRight)));
  ASSERT_TRUE(state->isOver());
}

TEST(GameStateSerde, RoundTripsAnAbandonedSeatAndEmptyPiles) {
  // A nameless seat is a real engine state (abandoned mid-game), and an
  // empty draw pile is the game-over-by-exhaustion shape.
  const Player abandoned{Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Three),
                         Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Five)};
  const Player named{"dana",
                     Card(Suit::Clubs, Rank::Six),
                     Card(Suit::Diamonds, Rank::Seven),
                     Card(Suit::Hearts, Rank::Eight),
                     Card(Suit::Spades, Rank::Nine),
                     {Position::TopLeft, Position::BottomRight},
                     true};
  const GameState state{{}, {}, {abandoned, named}, false, 1, 0, true, "g-ragged", "v-9"};
  expectRoundTrips(state);
}

// The frozen shape of a stored row. Everything else in this suite
// generates its bytes with the code under test, so only this test can
// catch a simultaneous serialize+deserialize change that would orphan —
// or worse, silently permute — rows already written. Do not regenerate
// this literal from the code; schema changes mean a version bump.
TEST(GameStateSerde, ParsesAFrozenV1Payload) {
  constexpr char kRow[] =
      R"({"discardPile":[43],"drawPile":[0,1],"peekedAtDrawPile":false,"peeksHidden":true,)"
      R"("players":[{"cards":[51,50,49,48],"donePeeking":true,"name":"a","peeked":[]},)"
      R"({"cards":[47,46,45,44],"donePeeking":true,"name":null,"peeked":[2]}],)"
      R"("v":1,"whoKnocked":1,"whoseTurn":1})";
  const auto restored = deserializeGameState(kRow);
  ASSERT_TRUE(restored.ok()) << restored.status();
  // Hand order is [tl, tr, bl, br]; a permutation here would round-trip
  // green everywhere else while scrambling every stored hand.
  EXPECT_EQ(restored->getPlayer(0).cardAt(Position::TopLeft), Card(51));
  EXPECT_EQ(restored->getPlayer(0).cardAt(Position::BottomRight), Card(48));
  EXPECT_EQ(restored->getDrawPile(), (std::deque<Card>{Card(0), Card(1)}));
  EXPECT_EQ(restored->getPlayer(1).getPeeked(), (std::vector<Position>{Position::BottomLeft}));
  EXPECT_FALSE(restored->getPlayer(1).getName().has_value());
  EXPECT_TRUE(restored->getGameId().empty());
  // Byte-for-byte re-serialization pins field names, sorted key order,
  // and compact formatting all at once.
  EXPECT_EQ(serializeGameState(*restored), kRow);
}

TEST(GameStateSerde, RejectsUnknownSchemaVersion) {
  json payload = dealtPayload();
  payload["v"] = 2;
  expectRejected(payload);
}

TEST(GameStateSerde, RejectsMalformedInput) {
  // Raw bytes on purpose — this is the one test where the string layer
  // itself is under test. All must read as invalid-argument, none may
  // crash: the bytes come from a database row, not from code we trust.
  const std::vector<std::string> bad = {
      "",                 // not JSON
      "not json at all",  // not JSON
      "[]",               // wrong top-level shape
      R"({"v":1})",       // missing everything else
      R"({"v":"1"})",     // version of the wrong type
      R"({"v":1.0})",     // floats are not versions
  };
  for (const std::string& input : bad) {
    const auto restored = deserializeGameState(input);
    ASSERT_FALSE(restored.ok()) << "accepted: " << input;
    EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument) << input;
  }
}

// get<int>() alone is a static_cast: 2^32+1 would wrap to 1 and pass the
// version gate, and 2^32 would wrap to card 0. The reads must reject
// before narrowing.
TEST(GameStateSerde, RejectsIntegersThatWouldWrapToValidValues) {
  json payload = dealtPayload();
  payload["v"] = int64_t{4294967297};
  expectRejected(payload);

  payload = dealtPayload();
  payload["drawPile"][0] = int64_t{4294967296};
  expectRejected(payload);

  payload = dealtPayload();
  payload["whoseTurn"] = int64_t{4294967296};
  expectRejected(payload);
}

TEST(GameStateSerde, RejectsEachFieldMissingOrMistyped) {
  const json good = dealtPayload();
  for (const auto& [key, value] : good.items()) {
    json without = good;
    without.erase(key);
    ASSERT_NO_FATAL_FAILURE(expectRejected(without)) << "missing " << key;
    json mistyped = good;
    mistyped[key] = json::object();
    ASSERT_NO_FATAL_FAILURE(expectRejected(mistyped)) << "mistyped " << key;
  }
  for (const auto& [key, value] : good["players"][0].items()) {
    json without = good;
    without["players"][0].erase(key);
    ASSERT_NO_FATAL_FAILURE(expectRejected(without)) << "missing player " << key;
    json mistyped = good;
    mistyped["players"][0][key] = json::object();
    ASSERT_NO_FATAL_FAILURE(expectRejected(mistyped)) << "mistyped player " << key;
  }
  json bare_player = good;
  bare_player["players"][0] = 5;
  expectRejected(bare_player);
}

TEST(GameStateSerde, RejectsOutOfRangeValues) {
  json payload = dealtPayload();
  payload["drawPile"][0] = 52;
  expectRejected(payload);

  payload = dealtPayload();
  payload["drawPile"][0] = -1;
  expectRejected(payload);

  payload = dealtPayload();
  payload["players"][0]["peeked"] = json::array({4});
  expectRejected(payload);
}

// The exact reject boundary: a two-seat game admits seats 0 and 1, so 2
// must fail — a loose value here would let an off-by-one in the range
// check send players.at() out of bounds in the engine.
TEST(GameStateSerde, RejectsSeatIndexAtTheBoundary) {
  json payload = dealtPayload();
  payload["whoseTurn"] = 2;
  expectRejected(payload);

  payload = dealtPayload();
  payload["whoKnocked"] = 2;
  expectRejected(payload);

  payload = dealtPayload();
  payload["whoKnocked"] = -2;
  expectRejected(payload);

  // An empty roster has no seat 0, and the engine's turn arithmetic
  // divides by the roster size.
  payload = dealtPayload();
  payload["players"] = json::array();
  payload["whoseTurn"] = 0;
  expectRejected(payload);
}

TEST(GameStateSerde, RejectsWrongHandSize) {
  json payload = dealtPayload();
  payload["players"][0]["cards"] = json::array({1, 2, 3});
  expectRejected(payload);

  payload = dealtPayload();
  payload["players"][0]["cards"] = json::array({1, 2, 3, 4, 5});
  expectRejected(payload);
}

// Pinned leniency, not oversight: serde polices only what the engine
// would index out of range; game legality (card uniqueness, peek caps)
// stays the engine's business, and unknown fields are tolerated on read
// so a rolled-back binary can load rows a newer one annotated — at the
// cost that re-serializing drops the extras.
TEST(GameStateSerde, ToleratesEngineLegalityAndUnknownFieldsByDesign) {
  json payload = dealtPayload();
  payload["players"][0]["cards"] = payload["players"][1]["cards"];  // duplicate cards
  EXPECT_TRUE(deserializeGameState(payload.dump()).ok());

  payload = dealtPayload();
  payload["players"][0]["peeked"] = json::array({0, 0, 1, 2});  // beyond the engine's peek cap
  EXPECT_TRUE(deserializeGameState(payload.dump()).ok());

  payload = dealtPayload();
  payload["annotation"] = "from-the-future";
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_TRUE(restored.ok());
  EXPECT_EQ(serializeGameState(*restored), dealtPayload().dump());
}

// Names are player-supplied; a byte sequence that is not UTF-8 must not
// take down the write path. Replacement changes the name's bytes, and
// the replaced form round-trips stably.
TEST(GameStateSerde, SerializesANameThatIsNotValidUtf8) {
  const Player mangled{std::string("bad\xff"), Card(0), Card(1), Card(2), Card(3)};
  const GameState state{{Card(4)}, {Card(5)}, {mangled, mangled}, false, 0, -1, false, "g", "v"};
  const std::string serialized = serializeGameState(state);
  const auto restored = deserializeGameState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(serializeGameState(*restored), serialized);
}

// A NUL byte is valid JSON but postgres jsonb (the step-2 storage
// column) refuses to store escaped NULs, so serialize replaces it
// like invalid UTF-8 — the bytes must never contain the escape.
TEST(GameStateSerde, SerializesANameContainingNulWithoutTheEscape) {
  const std::string nul_name("a\0b", 3);
  const Player mangled{nul_name, Card(0), Card(1), Card(2), Card(3)};
  const GameState state{{Card(4)}, {Card(5)}, {mangled, mangled}, false, 0, -1, false, "g", "v"};
  const std::string serialized = serializeGameState(state);
  EXPECT_EQ(serialized.find("\\u0000"), std::string::npos);
  const auto restored = deserializeGameState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->getPlayer(0).getName().value_or(""),
            "a\xEF\xBF\xBD"
            "b");
  EXPECT_EQ(serializeGameState(*restored), serialized);
}

}  // namespace
