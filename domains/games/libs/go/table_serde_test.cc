#include "domains/games/libs/go/table_serde.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace go {
namespace {

using nlohmann::json;

constexpr int64_t kT0 = 1'800'000'000'000;
constexpr TimeControl kClock{60'000, 2'000};

Table Open() { return *Table::open({"ann", "bo"}, 9, kDefaultKomi, kClock, kT0); }

Table Moved(const Table& table, int x, int y, int64_t at = kT0) {
  return *table.inGame(
      [&](const GameState& game) { return game.play(game.whoseTurn(), x, y, at); });
}

Table Passed(const Table& table) {
  return *table.inGame([](const GameState& game) { return game.pass(game.whoseTurn(), kT0); });
}

void ExpectSame(const Table& a, const Table& b) {
  EXPECT_EQ(a.players(), b.players());
  EXPECT_EQ(a.game().size(), b.game().size());
  EXPECT_EQ(a.game().komi(), b.game().komi());
  EXPECT_EQ(a.game().blackSeat(), b.game().blackSeat());
  EXPECT_EQ(a.game().moves(), b.game().moves());
  EXPECT_EQ(a.game().board(), b.game().board());
  EXPECT_EQ(a.game().clock(), b.game().clock());
  EXPECT_EQ(a.game().timeControl(), b.game().timeControl());
  EXPECT_EQ(a.game().result(), b.game().result());
  EXPECT_EQ(a.game().captures(Color::kBlack), b.game().captures(Color::kBlack));
  EXPECT_EQ(a.scoreSheet(), b.scoreSheet());
  EXPECT_EQ(a.isOver(), b.isOver());
  EXPECT_EQ(a.endedByClose(), b.endedByClose());
}

TEST(TableSerde, RoundTripsEveryShapeATableTakes) {
  // Black takes White's corner stone.
  const Table captured = Moved(Moved(Moved(Open(), 1, 0, kT0 + 500), 0, 0), 0, 1);
  const Table scored = Passed(Passed(captured));
  const Table even = Passed(Passed(*Table::open({"ann", "bo"}, 13, 0, kClock, kT0)));
  const Table flagged =
      *captured.inGame([](const GameState& game) { return game.flag(kT0 + 120'000); });
  for (const Table& shape :
       {Open(), captured, scored, even, flagged, *captured.removePlayer(0, kT0),
        *scored.removePlayer(0, kT0), *scored.next(19, -3.5, kClock, kT0 + 9)}) {
    const std::string stored = serializeTable(shape);
    const auto back = deserializeTable(stored);
    ASSERT_TRUE(back.ok()) << back.status() << "\n" << stored;
    ExpectSame(*back, shape);
    EXPECT_EQ(serializeTable(*back), stored);
  }
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  const json row = json::parse(serializeTable(Passed(Passed(Moved(Open(), 2, 1)))));
  EXPECT_EQ(row["v"], 1);
  EXPECT_EQ(row["players"], json::array({"ann", "bo"}));
  EXPECT_EQ(row["size"], 9);
  EXPECT_EQ(row["komi"], 6.5);
  EXPECT_EQ(row["blackSeat"], 0);
  EXPECT_EQ(row["moves"], json::array({11, -1, -1}));
  EXPECT_EQ(row["initialMs"], 60'000);
  EXPECT_EQ(row["incrementMs"], 2'000);
  EXPECT_EQ(row["remainingMs"], json::array({64'000, 62'000}));
  EXPECT_EQ(row["turnStartedMs"], kT0);
  EXPECT_EQ(row["result"], (json{{"ending", "score"}, {"winner", "black"}}));
  EXPECT_EQ(row["closed"], false);
  EXPECT_EQ(row["endedByClose"], false);
  EXPECT_EQ(row["scoreSheet"],
            json::array({{{"ending", "score"}, {"winner", "ann"}, {"winnerColor", "black"}}}));
}

TEST(TableSerde, StoresNoNulByte) {
  const std::string serialized =
      serializeTable(*Table::open({std::string("a\0n", 3), "bo"}, 9, kDefaultKomi, kClock, kT0));
  EXPECT_EQ(serialized.find('\0'), std::string::npos);
  EXPECT_EQ(serialized.find("\\u0000"), std::string::npos);
}

TEST(TableSerde, RefusesWhatPlayCouldNotReach) {
  const json good = json::parse(serializeTable(Moved(Open(), 4, 4)));
  EXPECT_TRUE(deserializeTable(good.dump()).ok());
  EXPECT_FALSE(deserializeTable("not json").ok());
  EXPECT_FALSE(deserializeTable("[]").ok());
  for (const char* field :
       {"v", "players", "size", "komi", "blackSeat", "moves", "initialMs", "incrementMs",
        "remainingMs", "turnStartedMs", "closed", "endedByClose", "scoreSheet"}) {
    json missing = good;
    missing.erase(field);
    EXPECT_FALSE(deserializeTable(missing.dump()).ok()) << field;
  }
  json occupied = good;
  occupied["moves"].push_back(40);
  EXPECT_FALSE(deserializeTable(occupied.dump()).ok());
  json scored = good;
  scored["result"] = json{{"ending", "score"}, {"winner", "black"}};
  EXPECT_FALSE(deserializeTable(scored.dump()).ok());
  json sized = good;
  sized["size"] = 11;
  EXPECT_FALSE(deserializeTable(sized.dump()).ok());
  json version = good;
  version["v"] = 2;
  EXPECT_FALSE(deserializeTable(version.dump()).ok());
}

}  // namespace
}  // namespace go
