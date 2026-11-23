#include "domains/games/libs/wordchain_race/table_serde.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace wordchain_race {
namespace {

const Dictionary& Dict() {
  static const Dictionary d = Dictionary::FromLines("cold\ncord\ncard\nward\nwarm\n");
  return d;
}

const Puzzle kPuzzle{"cold", "warm", 5};
constexpr int64_t kT0 = 1'000'000;

Table Played() {
  Table table = *Table::open({"ann", "bob"}, kPuzzle, kT0, 60'000);
  return *table.inRace([](const Race& race) -> absl::StatusOr<Race> {
    Race r = race;
    for (const char* word : {"cord", "card", "ward", "warm"}) {
      auto next = r.play(0, word, Dict(), kT0 + 500);
      if (!next.ok()) return next.status();
      r = *next;
    }
    auto next = r.play(1, "cord", Dict(), kT0 + 700);
    if (!next.ok()) return next.status();
    return next->expire();
  });
}

void ExpectSame(const Table& a, const Table& b) {
  EXPECT_EQ(a.race().puzzle(), b.race().puzzle());
  EXPECT_EQ(a.race().runners(), b.race().runners());
  EXPECT_EQ(a.race().startedMs(), b.race().startedMs());
  EXPECT_EQ(a.race().deadlineMs(), b.race().deadlineMs());
  EXPECT_EQ(a.race().timedOut(), b.race().timedOut());
  EXPECT_EQ(a.race().abandoned(), b.race().abandoned());
  EXPECT_EQ(a.scoreSheet(), b.scoreSheet());
  EXPECT_EQ(a.isOver(), b.isOver());
}

TEST(TableSerde, RoundTripsARaceInPlay) {
  Table table = *Table::open({"ann", "bob", "cy"}, kPuzzle, kT0, 60'000);
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
}

TEST(TableSerde, RoundTripsAScoredRaceWithItsFinishes) {
  Table table = Played();
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
  EXPECT_EQ(back->race().winner(), "ann");
}

TEST(TableSerde, RoundTripsAClosedAbandonedTable) {
  Table table = *Table::open({"ann", "bob"}, kPuzzle, kT0, 60'000)->removePlayer(0);
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  nlohmann::json j = nlohmann::json::parse(serializeTable(Played()));
  EXPECT_EQ(j["v"], 1);
  EXPECT_EQ(j["puzzle"]["start"], "cold");
  EXPECT_EQ(j["runners"][0]["id"], "ann");
  EXPECT_EQ(j["runners"][0]["finishedMs"], kT0 + 500);
  EXPECT_FALSE(j["runners"][1].contains("finishedMs"));
  EXPECT_EQ(j["scoreSheet"][0]["winner"], "ann");
  EXPECT_EQ(j["timedOut"], true);
  EXPECT_EQ(j["closed"], false);
}

TEST(TableSerde, RefusesAnotherVersionMalformedJsonOrAnUnplayableRow) {
  EXPECT_FALSE(deserializeTable("not json").ok());
  nlohmann::json j = nlohmann::json::parse(serializeTable(Played()));
  j["v"] = 2;
  EXPECT_FALSE(deserializeTable(j.dump()).ok());
  j["v"] = 1;
  j["runners"][1]["ladder"] = {"warm"};
  EXPECT_FALSE(deserializeTable(j.dump()).ok());
  j = nlohmann::json::parse(serializeTable(Played()));
  j.erase("deadlineMs");
  EXPECT_FALSE(deserializeTable(j.dump()).ok());
}

}  // namespace
}  // namespace wordchain_race
