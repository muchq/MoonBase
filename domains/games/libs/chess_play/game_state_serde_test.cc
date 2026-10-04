#include "domains/games/libs/chess_play/game_state_serde.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {
namespace {

using nlohmann::json;

constexpr char kKpk[] = "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1";
constexpr int64_t kT0 = 1'000'000;

GameState Started() {
  auto state = GameState::start({"alice", "bob"}, "kpk", Opening{kKpk, 1}, {180'000, 2'000}, kT0,
                                "kpk-opposition");
  EXPECT_TRUE(state.ok()) << state.status();
  return *state;
}

GameState Played() {
  auto one = Started().move(1, "e2e4", kT0 + 4'000);
  EXPECT_TRUE(one.ok());
  auto two = one->move(0, "e5e6", kT0 + 9'000);
  EXPECT_TRUE(two.ok());
  return *two;
}

void ExpectRoundTrips(const GameState& state) {
  const std::string bytes = serializeGameState(state);
  const auto restored = deserializeGameState(bytes);
  ASSERT_TRUE(restored.ok()) << restored.status() << "\n" << bytes;
  EXPECT_EQ(restored->players(), state.players());
  EXPECT_EQ(restored->variant(), state.variant());
  EXPECT_EQ(restored->setupId(), state.setupId());
  EXPECT_EQ(restored->whiteSeat(), state.whiteSeat());
  EXPECT_EQ(restored->startFen(), state.startFen());
  EXPECT_EQ(restored->moves(), state.moves());
  EXPECT_EQ(restored->timeControl(), state.timeControl());
  EXPECT_EQ(restored->clock(), state.clock());
  EXPECT_EQ(restored->result(), state.result());
  EXPECT_EQ(restored->fen(), state.fen());
  EXPECT_EQ(serializeGameState(*restored), bytes);
}

void ExpectRejected(const json& payload) {
  const auto restored = deserializeGameState(payload.dump());
  ASSERT_FALSE(restored.ok()) << "accepted: " << payload.dump();
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ChessSerde, EveryStageRoundTrips) {
  ExpectRoundTrips(Started());
  ExpectRoundTrips(Played());
  ExpectRoundTrips(*Played().resign(0, kT0 + 10'000));
  ExpectRoundTrips(*Played().removePlayer(1, kT0 + 10'000));
  // A draw on time: no winner.
  ExpectRoundTrips(*Started().flag(kT0 + 180'000));
  auto mated = GameState::start({"a", "b"}, "kpk", Opening{"7k/8/6K1/8/8/8/8/1Q6 w - - 0 1", 0},
                                {60'000, 0}, kT0, std::string(kRandomKpkSetup));
  ASSERT_TRUE(mated.ok());
  ExpectRoundTrips(*mated->move(0, "b1b8", kT0));
}

// The exact bytes a game two moves in stores. A change here is a schema
// change and means a version bump, not an edit to this string.
TEST(ChessSerde, TheStoredBytesArePinned) {
  EXPECT_EQ(serializeGameState(Played()),
            R"({"clock":{"blackMs":177000,"turnStartedMs":1009000,"whiteMs":178000},)"
            R"("moves":["e2e4","e5e6"],"players":["alice","bob"],)"
            R"("setupId":"kpk-opposition",)"
            R"("startFen":"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1",)"
            R"("timeControl":{"incrementMs":2000,"initialMs":180000},"v":2,)"
            R"("variant":"kpk","whiteSeat":1})");
  EXPECT_EQ(json::parse(serializeGameState(*Played().resign(1, kT0 + 10'000)))["result"],
            json::parse(R"({"winner":"black","ending":"resignation"})"));
  EXPECT_FALSE(
      json::parse(serializeGameState(*Started().flag(kT0 + 180'000)))["result"].contains("winner"));
}

TEST(ChessSerde, VersionOneRowsRestoreAsTheOriginalRandomKpkSetup) {
  const std::string legacy =
      R"({"clock":{"blackMs":177000,"turnStartedMs":1009000,"whiteMs":178000},)"
      R"("moves":["e2e4","e5e6"],"players":["alice","bob"],)"
      R"("startFen":"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1",)"
      R"("timeControl":{"incrementMs":2000,"initialMs":180000},"v":1,)"
      R"("variant":"kpk","whiteSeat":1})";
  const auto restored = deserializeGameState(legacy);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->setupId(), kRandomKpkSetup);
}

TEST(ChessSerde, APlayerIdJsonbWouldRefuseIsReplaced) {
  auto state =
      GameState::start({std::string("a\0b", 3), "\xff"}, "kpk", Opening{kKpk, 0}, {60'000, 0}, kT0,
                       std::string(kRandomKpkSetup));
  ASSERT_TRUE(state.ok());
  const auto restored = deserializeGameState(serializeGameState(*state));
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->players(), (std::vector<std::string>{"a\xEF\xBF\xBD"
                                                           "b",
                                                           "\xEF\xBF\xBD"}));
}

TEST(ChessSerde, RefusesWhatIsNotAStoredGame) {
  const json good = json::parse(serializeGameState(Played()));
  ASSERT_TRUE(deserializeGameState(good.dump()).ok());  // the control

  EXPECT_FALSE(deserializeGameState("not json").ok());
  EXPECT_FALSE(deserializeGameState("[]").ok());

  const auto without = [&](const char* key) {
    json payload = good;
    payload.erase(key);
    return payload;
  };
  for (const char* key :
       {"v", "players", "variant", "setupId", "whiteSeat", "startFen", "moves", "timeControl",
        "clock"}) {
    ExpectRejected(without(key));
  }

  const auto with = [&](const json::json_pointer& at, json value) {
    json payload = good;
    payload[at] = std::move(value);
    return payload;
  };
  ExpectRejected(with("/v"_json_pointer, 3));
  ExpectRejected(with("/players"_json_pointer, json::array({"alice"})));
  ExpectRejected(with("/players/0"_json_pointer, 7));
  ExpectRejected(with("/variant"_json_pointer, "atomic"));
  ExpectRejected(with("/setupId"_json_pointer, "not-a-setup"));
  ExpectRejected(with("/whiteSeat"_json_pointer, 2));
  ExpectRejected(with("/whiteSeat"_json_pointer, "1"));
  ExpectRejected(with("/startFen"_json_pointer, "nonsense"));
  ExpectRejected(with("/moves"_json_pointer, json::array({"e2e4", "e5e5"})));
  ExpectRejected(with("/moves/0"_json_pointer, 42));
  ExpectRejected(with("/timeControl/initialMs"_json_pointer, 0));
  ExpectRejected(with("/timeControl/incrementMs"_json_pointer, 1.5));
  ExpectRejected(with("/clock/whiteMs"_json_pointer, -1));
  ExpectRejected(with("/clock/turnStartedMs"_json_pointer, "later"));
  // Past int64: nlohmann reads it unsigned, and it must not wrap.
  ExpectRejected(
      with("/clock/turnStartedMs"_json_pointer, json(uint64_t{18'000'000'000'000'000'000u})));
  ExpectRejected(with("/result"_json_pointer, json::parse(R"({"ending":"sulking"})")));
  ExpectRejected(
      with("/result"_json_pointer, json::parse(R"({"winner":"red","ending":"timeout"})")));
  // An ending the board would have to reach, on a board that has not.
  ExpectRejected(
      with("/result"_json_pointer, json::parse(R"({"winner":"white","ending":"checkmate"})")));
}

}  // namespace
}  // namespace chess_play
