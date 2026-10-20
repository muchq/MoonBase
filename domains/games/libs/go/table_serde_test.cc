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

Table Accepted(const Table& table, int seat) {
  return *table.inGame([seat](const GameState& game) { return game.accept(seat, kT0); });
}

Table Scored(const Table& table) { return Accepted(Accepted(Passed(Passed(table)), 0), 1); }

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
  EXPECT_EQ(a.game().dead(), b.game().dead());
  EXPECT_EQ(a.game().accepted(), b.game().accepted());
  EXPECT_EQ(a.game().captures(Color::kBlack), b.game().captures(Color::kBlack));
  EXPECT_EQ(a.scoreSheet(), b.scoreSheet());
  EXPECT_EQ(a.isOver(), b.isOver());
  EXPECT_EQ(a.endedByClose(), b.endedByClose());
}

TEST(TableSerde, RoundTripsEveryShapeATableTakes) {
  // Black takes White's corner stone.
  const Table captured = Moved(Moved(Moved(Open(), 1, 0, kT0 + 500), 0, 0), 0, 1);
  const Table scored = Scored(captured);
  const Table even = Scored(*Table::open({"ann", "bo"}, 13, 0, kClock, kT0));
  // White's lone stone at (5,5) marked dead, and Black's acceptance.
  const Table marked = Accepted(
      *Passed(Passed(Moved(captured, 5, 5))).inGame([](const GameState& game) {
        return game.markDead(1, 5, 5, kT0);
      }),
      0);
  const Table flagged =
      *captured.inGame([](const GameState& game) { return game.flag(kT0 + 120'000); });
  for (const Table& shape :
       {Open(), captured, scored, even, marked, Scored(Moved(captured, 5, 5)), flagged, *captured.removePlayer(0, kT0),
        *scored.removePlayer(0, kT0), *scored.next(19, -3.5, kClock, kT0 + 9)}) {
    const std::string stored = serializeTable(shape);
    const auto back = deserializeTable(stored);
    ASSERT_TRUE(back.ok()) << back.status() << "\n" << stored;
    ExpectSame(*back, shape);
    EXPECT_EQ(serializeTable(*back), stored);
  }
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  const json row = json::parse(serializeTable(Scored(Moved(Open(), 2, 1))));
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
  EXPECT_FALSE(row.contains("dead")) << "written only once a stone is marked";
  EXPECT_EQ(row["accepted"], json::array({true, true}));
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

TEST(TableSerde, WritesTheMarksAndAcceptancesWhileScoring) {
  const Table marked = Accepted(
      *Passed(Passed(Moved(Moved(Open(), 4, 4), 0, 0))).inGame([](const GameState& game) {
        return game.markDead(0, 0, 0, kT0);
      }),
      1);
  const json row = json::parse(serializeTable(marked));
  EXPECT_EQ(row["dead"], json::array({0}));
  EXPECT_EQ(row["accepted"], json::array({false, true}));
}

// A row from before marking: two passes and a score, no marks written.
TEST(TableSerde, ReadsARowScoredBeforeMarking) {
  json row = json::parse(serializeTable(Scored(Moved(Open(), 2, 1))));
  ASSERT_FALSE(row.contains("dead"));
  row.erase("accepted");
  const auto back = deserializeTable(row.dump());
  ASSERT_TRUE(back.ok()) << back.status();
  EXPECT_EQ(back->game().result(), (Result{Color::kBlack, Ending::kScore}));
  row["dead"] = json::array({"x"});
  EXPECT_FALSE(deserializeTable(row.dump()).ok());
}

// A finished game as the archive keeps it: the marks, the acceptances
// and the result, read back through GameState::restore.
TEST(GameStateSerde, RoundTripsAMarkedGameAsTheTableRowLessTableFields) {
  const Table marked = Accepted(
      Accepted(*Passed(Passed(Moved(Moved(Open(), 4, 4), 0, 0))).inGame([](const GameState& game) {
        return game.markDead(0, 0, 0, kT0);
      }),
               1),
      0);
  const GameState& game = marked.game();
  ASSERT_TRUE(game.isOver());
  const std::string serialized = serializeGameState(game);
  const auto back = deserializeGameState(serialized);
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*Table::restore(*back, marked.scoreSheet(), false, false), marked);

  json expected = json::parse(serializeTable(marked));
  for (const char* table_field : {"closed", "endedByClose", "scoreSheet"}) {
    expected.erase(table_field);
  }
  EXPECT_EQ(json::parse(serialized), expected);
}

TEST(GameStateSerde, RefusesWhatPlayCouldNotReach) {
  json game = json::parse(serializeGameState(Moved(Open(), 4, 4).game()));
  EXPECT_TRUE(deserializeGameState(game.dump()).ok());  // the control
  game["moves"].push_back(40);
  EXPECT_FALSE(deserializeGameState(game.dump()).ok());
  EXPECT_FALSE(deserializeGameState("not json").ok());
}

}  // namespace
}  // namespace go
