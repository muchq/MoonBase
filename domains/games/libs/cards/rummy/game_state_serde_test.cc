#include "domains/games/libs/cards/rummy/game_state_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/game_state.h"

using namespace cards;
using namespace rummy;
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
  const auto state = dealRummyGame("g", {"a", "b"}, pristineDeck());
  EXPECT_TRUE(state.ok()) << state.status();
  return *state;
}

// A known-good serialized game as an editable payload; negative tests
// corrupt exactly the field they name and nothing else.
json dealtPayload() { return json::parse(serializeGameState(dealt())); }

// Mid-turn, with a meld down and the discard's top in hand: every
// optional field present.
GameState midTurn() {
  auto drew = dealt().drawDiscard(0);  // 9♠ onto A♠ ... 10♠
  EXPECT_TRUE(drew.ok()) << drew.status();
  auto melded = drew->meld(0, {Card{Suit::Spades, Rank::Ace}, Card{Suit::Spades, Rank::King},
                               Card{Suit::Spades, Rank::Queen}});
  EXPECT_TRUE(melded.ok()) << melded.status();
  return *melded;
}

json midTurnPayload() { return json::parse(serializeGameState(midTurn())); }

void expectRejected(const json& payload) {
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

// Engine truth only: identity is the storage row's business, so the
// serde returns it empty and the comparison excludes it.
void expectStatesEqual(const GameState& a, const GameState& b) {
  EXPECT_EQ(a.getStock(), b.getStock());
  EXPECT_EQ(a.getDiscard(), b.getDiscard());
  EXPECT_EQ(a.getPlayers(), b.getPlayers());
  EXPECT_EQ(a.getMelds(), b.getMelds());
  EXPECT_EQ(a.getWhoseTurn(), b.getWhoseTurn());
  EXPECT_EQ(a.getStage(), b.getStage());
  EXPECT_EQ(a.getPhase(), b.getPhase());
  EXPECT_EQ(a.getTakenDiscard(), b.getTakenDiscard());
  EXPECT_EQ(a.getLastMove(), b.getLastMove());
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

TEST(RummySerde, ADealtGameRoundTrips) { expectRoundTrips(dealt()); }

// Every move kind, both stages and every ending, reached by play.
TEST(RummySerde, EveryMoveAndPhaseRoundTrips) {
  auto stock = dealt().drawStock(0);
  ASSERT_TRUE(stock.ok());
  expectRoundTrips(*stock);
  auto discarded = stock->discard(0, Card{Suit::Spades, Rank::Ace});
  ASSERT_TRUE(discarded.ok()) << discarded.status();
  expectRoundTrips(*discarded);

  const GameState mid = midTurn();
  expectRoundTrips(mid);
  auto laid = mid.layOff(0, Card{Suit::Spades, Rank::Jack}, 0);
  ASSERT_TRUE(laid.ok()) << laid.status();
  expectRoundTrips(*laid);

  auto abandoned = mid.removePlayer(1);
  ASSERT_TRUE(abandoned.ok());
  ASSERT_EQ(abandoned->getPhase(), Phase::Abandoned);
  expectRoundTrips(*abandoned);

  // Alice's deal is two runs short of the discard's 9♠: taking it, she
  // lays her whole hand down and wins.
  auto run = laid->meld(0, {Card{Suit::Spades, Rank::Ten}, Card{Suit::Spades, Rank::Nine}});
  EXPECT_FALSE(run.ok());
  auto ten = laid->layOff(0, Card{Suit::Spades, Rank::Ten}, 0);
  ASSERT_TRUE(ten.ok()) << ten.status();
  auto nine = ten->layOff(0, Card{Suit::Spades, Rank::Nine}, 0);
  ASSERT_TRUE(nine.ok()) << nine.status();
  auto over = nine->meld(0, {Card{Suit::Diamonds, Rank::Ace}, Card{Suit::Diamonds, Rank::King},
                             Card{Suit::Diamonds, Rank::Queen}, Card{Suit::Diamonds, Rank::Jack},
                             Card{Suit::Diamonds, Rank::Ten}});
  ASSERT_TRUE(over.ok()) << over.status();
  ASSERT_EQ(over->getPhase(), Phase::Over);
  expectRoundTrips(*over);
}

// The exact bytes a fresh two-seat deal stores. A change to the shape
// here is a schema change and means a version bump, not an edit to this
// literal.
TEST(RummySerde, FrozenPayload) {
  constexpr const char* kRow =
      R"({"discard":[31],"melds":[],"phase":"playing",)"
      R"("players":[{"hand":[51,49,47,45,43,41,39,37,35,33],"id":"a"},)"
      R"({"hand":[50,48,46,44,42,40,38,36,34,32],"id":"b"}],"stage":"draw",)"
      R"("stock":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30],)"
      R"("v":1,"whoseTurn":0})";
  EXPECT_EQ(serializeGameState(dealt()), kRow);
  const auto restored = deserializeGameState(kRow);
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(dealt(), *restored);
}

// The bytes of a row mid-turn: the optional fields and the other stage's
// and move's spellings, which a rename would orphan.
TEST(RummySerde, FrozenMidTurnPayload) {
  constexpr const char* kRow =
      R"({"discard":[],"lastMove":{"cards":[43,47,51],"kind":"meld","meld":0,"player":"a"},)"
      R"("melds":[{"cards":[43,47,51],"owner":"a"}],"phase":"playing",)"
      R"("players":[{"hand":[49,45,41,39,37,35,33,31],"id":"a"},)"
      R"({"hand":[50,48,46,44,42,40,38,36,34,32],"id":"b"}],"stage":"play",)"
      R"("stock":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30],)"
      R"("takenDiscard":31,"v":1,"whoseTurn":0})";
  EXPECT_EQ(serializeGameState(midTurn()), kRow);
  for (const auto& [kind, want] :
       std::vector<std::pair<const char*, MoveKind>>{{"drawStock", MoveKind::DrawStock},
                                                     {"drawDiscard", MoveKind::DrawDiscard},
                                                     {"layOff", MoveKind::LayOff},
                                                     {"discard", MoveKind::Discard}}) {
    json payload = json::parse(kRow);
    payload["lastMove"]["kind"] = kind;
    payload["lastMove"]["cards"] = want == MoveKind::DrawStock ? json::array() : json::array({31});
    payload["lastMove"]["meld"] = want == MoveKind::LayOff ? 0 : -1;
    const auto restored = deserializeGameState(payload.dump());
    ASSERT_TRUE(restored.ok()) << kind;
    EXPECT_EQ(restored->getLastMove()->kind, want);
  }
  for (const char* phase : {"over", "abandoned"}) {
    json payload = json::parse(kRow);
    payload["phase"] = phase;
    payload["whoseTurn"] = -1;
    const auto ended = deserializeGameState(payload.dump());
    ASSERT_TRUE(ended.ok()) << phase;
    EXPECT_TRUE(ended->isOver());
  }
}

TEST(RummySerde, RejectsWhatTheEngineWouldIndexOutOfRange) {
  json payload = dealtPayload();
  payload["v"] = 2;
  expectRejected(payload);

  payload = dealtPayload();
  payload["whoseTurn"] = 2;  // two seats: 0..1 in play
  expectRejected(payload);
  // A playing row must name a seat; no turn is the end's.
  payload = dealtPayload();
  payload["whoseTurn"] = -1;
  expectRejected(payload);
  payload["phase"] = "over";
  EXPECT_TRUE(deserializeGameState(payload.dump()).ok());
  payload["whoseTurn"] = -2;
  expectRejected(payload);

  // Integers read as int64 before narrowing: 2^32+1 must not wrap to 1.
  payload = dealtPayload();
  payload["whoseTurn"] = 4294967297;
  expectRejected(payload);
  payload = dealtPayload();
  payload["players"][0]["hand"][0] = 4294967297;
  expectRejected(payload);

  for (const char* key : {"id", "hand"}) {
    payload = dealtPayload();
    payload["players"][0].erase(key);
    expectRejected(payload);
    payload = dealtPayload();
    payload["players"][0][key] = 7;
    expectRejected(payload);
  }
  for (const char* key : {"stock", "discard", "melds", "players", "stage", "phase"}) {
    payload = dealtPayload();
    payload.erase(key);
    expectRejected(payload);
  }

  payload = dealtPayload();
  payload["stock"][0] = 52;
  expectRejected(payload);
  payload = dealtPayload();
  payload["discard"][0] = -1;
  expectRejected(payload);
  payload = dealtPayload();
  payload["phase"] = "dealing";
  expectRejected(payload);
  payload = dealtPayload();
  payload["stage"] = "discard";
  expectRejected(payload);
  payload = dealtPayload();
  payload["players"] = json::array();
  expectRejected(payload);

  payload = midTurnPayload();
  payload["melds"][0].erase("owner");
  expectRejected(payload);
  payload = midTurnPayload();
  payload["melds"][0]["cards"][0] = 99;
  expectRejected(payload);
  payload = midTurnPayload();
  payload["takenDiscard"] = 52;
  expectRejected(payload);
  payload = midTurnPayload();
  payload["takenDiscard"] = "9S";
  expectRejected(payload);

  // A last move names a player, a kind it knows, its cards, and a meld
  // that is on the table or -1.
  payload = midTurnPayload();
  payload["lastMove"] = 7;
  expectRejected(payload);
  payload = midTurnPayload();
  payload["lastMove"]["kind"] = "knock";
  expectRejected(payload);
  payload = midTurnPayload();
  payload["lastMove"]["meld"] = 1;
  expectRejected(payload);
  payload = midTurnPayload();
  payload["lastMove"]["meld"] = -2;
  expectRejected(payload);
  payload = midTurnPayload();
  payload["lastMove"].erase("player");
  expectRejected(payload);
  payload = midTurnPayload();
  payload["lastMove"]["cards"][0] = 52;
  expectRejected(payload);

  for (const char* input : {"", "[]", "not json", R"({"v":1})", R"({"v":"1"})"}) {
    EXPECT_FALSE(deserializeGameState(input).ok()) << input;
  }
}

// A last move's cards and meld follow from its kind — the shape the
// engine writes. Anything else would be sent to every chair as it stands:
// a stock draw naming a card would show the table a card nobody saw.
TEST(RummySerde, ALastMoveMustHaveItsKindsShape) {
  auto with_move = [](const char* kind, json cards, int meld) {
    json payload = midTurnPayload();
    payload["lastMove"] = json{{"player", "a"}, {"kind", kind}, {"cards", cards}, {"meld", meld}};
    return payload;
  };
  expectRejected(with_move("drawStock", json::array({3}), -1));
  expectRejected(with_move("drawStock", json::array(), 0));
  expectRejected(with_move("drawDiscard", json::array(), -1));
  expectRejected(with_move("drawDiscard", json::array({3, 4}), -1));
  expectRejected(with_move("discard", json::array({3}), 0));
  expectRejected(with_move("meld", json::array({43, 47, 51}), -1));
  expectRejected(with_move("meld", json::array({43, 47}), 0));
  expectRejected(with_move("layOff", json::array({3}), -1));
  expectRejected(with_move("layOff", json::array({3, 4}), 0));
  // The twins, each the shape the engine writes.
  for (const auto& payload :
       {with_move("drawStock", json::array(), -1), with_move("drawDiscard", json::array({3}), -1),
        with_move("discard", json::array({3}), -1), with_move("meld", json::array({43, 47, 51}), 0),
        with_move("layOff", json::array({3}), 0)}) {
    EXPECT_TRUE(deserializeGameState(payload.dump()).ok()) << payload.dump();
  }
}

// A table in play must have a move to make: a draw with nothing to draw
// from, or a play stage with no card to put down, would hold every seat
// at "not your turn" until they all left. Such a row is dropped instead.
TEST(RummySerde, APlayingRowWithNoMoveToMakeIsRejected) {
  json payload = dealtPayload();
  payload["stock"] = json::array();
  payload["discard"] = json::array();
  expectRejected(payload);
  // Its twin: the empty stock with a discard to take is a table.
  payload["discard"] = json::array({31});
  EXPECT_TRUE(deserializeGameState(payload.dump()).ok());

  payload = midTurnPayload();
  payload["players"][0]["hand"] = json::array();
  expectRejected(payload);
  // Over, an empty hand is the winner's.
  payload["phase"] = "over";
  payload["whoseTurn"] = -1;
  EXPECT_TRUE(deserializeGameState(payload.dump()).ok());
}

TEST(RummySerde, UnknownFieldsAreIgnored) {
  json payload = dealtPayload();
  payload["future"] = "field";
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(dealt(), *restored);
}

// A NUL in a player id would fail the postgres write; it is replaced,
// not fatal — wherever the id is written.
TEST(RummySerde, NulInAPlayerIdIsReplaced) {
  const std::string nul_id("a\0b", 3);
  auto state = dealRummyGame("g", {nul_id, "b"}, pristineDeck());
  ASSERT_TRUE(state.ok());
  auto drew = state->drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  auto melded = drew->meld(0, {Card{Suit::Spades, Rank::Ace}, Card{Suit::Spades, Rank::King},
                               Card{Suit::Spades, Rank::Queen}});
  ASSERT_TRUE(melded.ok());
  const std::string serialized = serializeGameState(*melded);
  EXPECT_EQ(serialized.find('\0'), std::string::npos);
  const json parsed = json::parse(serialized);
  const std::string replaced =
      "a\xEF\xBF\xBD"
      "b";
  EXPECT_EQ(parsed["players"][0]["id"], replaced);
  EXPECT_EQ(parsed["melds"][0]["owner"], replaced);
  EXPECT_EQ(parsed["lastMove"]["player"], replaced);
  ASSERT_TRUE(deserializeGameState(serialized).ok());
}

}  // namespace
