#include "domains/games/libs/chess_play/table_serde.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/game_state_serde.h"
#include "domains/games/libs/chess_play/table.h"

namespace chess_play {
namespace {

using nlohmann::json;

constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";
constexpr int64_t kT0 = 1'000'000;

Table Opened() { return *Table::open({"alice", "bob"}, "kpk", Opening{kMate, 0}, {60'000, 0}, kT0); }
Table Mated(const Table& table) {
  const int white = table.game().whiteSeat();
  return *table.inGame([&](const GameState& game) { return game.move(white, "e7e8q", kT0); });
}

void ExpectRoundTrips(const Table& table) {
  const std::string bytes = serializeTable(table);
  const auto restored = deserializeTable(bytes);
  ASSERT_TRUE(restored.ok()) << restored.status() << "\n" << bytes;
  EXPECT_EQ(restored->scoreSheet(), table.scoreSheet());
  EXPECT_EQ(restored->isOver(), table.isOver());
  EXPECT_EQ(restored->endedByClose(), table.endedByClose());
  EXPECT_EQ(serializeGameState(restored->game()), serializeGameState(table.game()));
  EXPECT_EQ(serializeTable(*restored), bytes);
}

TEST(ChessTableSerde, EveryStageRoundTrips) {
  ExpectRoundTrips(Opened());
  ExpectRoundTrips(Mated(Opened()));
  ExpectRoundTrips(*Mated(Opened()).next(Opening{kMate, 0}, {60'000, 0}, kT0));
  ExpectRoundTrips(*Opened().removePlayer(0, kT0));
  ExpectRoundTrips(*Mated(Opened()).removePlayer(0, kT0));
}

// A schema change means a version bump, not an edit to this string.
TEST(ChessTableSerde, TheStoredBytesArePinned) {
  const Table table = Mated(Opened());
  EXPECT_EQ(serializeTable(table),
            R"({"closed":false,"endedByClose":false,"game":)" + serializeGameState(table.game()) +
                R"(,"scoreSheet":[{"ending":"checkmate","winner":"alice"}],"v":2})");
}

// A row stored before tables: one game, which is the table.
TEST(ChessTableSerde, AGameRowReadsAsItsTable) {
  const auto live = deserializeTable(serializeGameState(Opened().game()));
  ASSERT_TRUE(live.ok()) << live.status();
  EXPECT_FALSE(live->isOver());
  EXPECT_TRUE(live->scoreSheet().empty());

  // A finished one was a table the finish closed.
  const auto finished = deserializeTable(serializeGameState(Mated(Opened()).game()));
  ASSERT_TRUE(finished.ok()) << finished.status();
  EXPECT_TRUE(finished->isOver());
  EXPECT_TRUE(finished->endedByClose());
  EXPECT_EQ(finished->scoreSheet(), Mated(Opened()).scoreSheet());
}

TEST(ChessTableSerde, RefusesWhatIsNotAStoredTable) {
  const json good = json::parse(serializeTable(Mated(Opened())));
  ASSERT_TRUE(deserializeTable(good.dump()).ok());  // the control
  EXPECT_FALSE(deserializeTable("not json").ok());
  for (const char* key : {"closed", "endedByClose", "game", "scoreSheet"}) {
    json payload = good;
    payload.erase(key);
    EXPECT_FALSE(deserializeTable(payload.dump()).ok()) << key;
  }
  const auto with = [&](const json::json_pointer& at, json value) {
    json payload = good;
    payload[at] = std::move(value);
    return deserializeTable(payload.dump());
  };
  EXPECT_FALSE(with("/v"_json_pointer, 3).ok());
  EXPECT_FALSE(with("/scoreSheet/0/ending"_json_pointer, "sulking").ok());
  EXPECT_FALSE(with("/scoreSheet/0/winner"_json_pointer, 7).ok());
  // What restore refuses, the row does too.
  EXPECT_FALSE(with("/scoreSheet"_json_pointer, json::array()).ok());
  EXPECT_FALSE(with("/game/moves"_json_pointer, json::array({"e7e5"})).ok());
}

}  // namespace
}  // namespace chess_play
