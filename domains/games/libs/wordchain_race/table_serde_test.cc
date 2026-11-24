#include "domains/games/libs/wordchain_race/table_serde.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace wordchain_race {
namespace {

const Dictionary& Dict() {
  static const Dictionary d = Dictionary::FromLines("cold\ncord\ncard\nward\nwart\nwarm\n");
  return d;
}

const Puzzle kPuzzle{"cold", "warm", 5, "enable"};
constexpr int64_t kT0 = 1'000'000;

absl::StatusOr<Race> Play(Race race, int seat, const std::vector<std::string>& words) {
  for (const std::string& word : words) {
    auto next = race.play(seat, word, Dict(), kT0 + 500);
    if (!next.ok()) return next.status();
    race = *next;
  }
  return race;
}

// ann finishes, cy leaves, bob is caught by the clock.
Table Played() {
  Table table = *Table::open({"ann", "bob", "cy"}, kPuzzle, kT0, 60'000);
  table = *table.removePlayer(2);
  return *table.inRace([](const Race& race) -> absl::StatusOr<Race> {
    auto r = Play(race, 0, {"cord", "card", "ward", "wart", "warm"});
    if (!r.ok()) return r.status();
    r = Play(*r, 1, {"cord"});
    if (!r.ok()) return r.status();
    return r->expire(kT0 + 60'000);
  });
}

void ExpectSame(const Table& a, const Table& b) {
  EXPECT_EQ(a.race().puzzle(), b.race().puzzle());
  EXPECT_EQ(a.race().runners(), b.race().runners());
  EXPECT_EQ(a.race().startedMs(), b.race().startedMs());
  EXPECT_EQ(a.race().deadlineMs(), b.race().deadlineMs());
  EXPECT_EQ(a.race().ending(), b.race().ending());
  EXPECT_EQ(a.scoreSheet(), b.scoreSheet());
  EXPECT_EQ(a.isOver(), b.isOver());
  EXPECT_EQ(a.endedByClose(), b.endedByClose());
}

TEST(TableSerde, RoundTripsARaceInPlay) {
  Table table = *Table::open({"ann", "bob", "cy"}, kPuzzle, kT0, 60'000);
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
}

TEST(TableSerde, RoundTripsAScoredRaceWithItsFinishesAndLeaver) {
  Table table = Played();
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
  EXPECT_EQ(back->race().winners(), (std::vector<std::string>{"ann"}));
}

TEST(TableSerde, RoundTripsATableClosedByLeave) {
  Table table = *Table::open({"ann", "bob"}, kPuzzle, kT0, 60'000)->removePlayer(0);
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
  EXPECT_EQ(back->race().ending(), Ending::kLeave);
  EXPECT_TRUE(back->endedByClose());
}

// A leave after the clock ended a race leaves every runner done; the row
// still reads back as the timeout it was.
TEST(TableSerde, ALeaveAfterATimeoutStillReadsBack) {
  Table table = Played();
  table = *table.removePlayer(1);
  ASSERT_EQ(table.race().ending(), Ending::kTimeout);
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
}

TEST(TableSerde, ALeaveDownToOneAfterATimeoutStillReadsBack) {
  Table table = *Table::open({"ann", "bob"}, kPuzzle, kT0, 60'000);
  table = *table.inRace([](const Race& race) -> absl::StatusOr<Race> {
    auto r = Play(race, 0, {"cord", "card", "ward", "wart", "warm"});
    if (!r.ok()) return r.status();
    return r->expire(kT0 + 60'000);
  });
  table = *table.removePlayer(1);
  ASSERT_TRUE(table.isOver());
  auto back = deserializeTable(serializeTable(table));
  ASSERT_TRUE(back.ok()) << back.status();
  ExpectSame(*back, table);
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  const nlohmann::json expected = {
      {"v", 1},
      {"puzzle", {{"start", "cold"}, {"target", "warm"}, {"optimal", 5}, {"lexicon", "enable"}}},
      {"runners",
       {{{"id", "ann"},
         {"ladder", {"cord", "card", "ward", "wart", "warm"}},
         {"finishedMs", kT0 + 500},
         {"left", false}},
        {{"id", "bob"}, {"ladder", {"cord"}}, {"left", false}},
        {{"id", "cy"}, {"ladder", nlohmann::json::array()}, {"left", true}}}},
      {"startedMs", kT0},
      {"deadlineMs", kT0 + 60'000},
      {"ending", "timeout"},
      {"scoreSheet", {{{"winners", {"ann"}}, {"ending", "timeout"}}}},
      {"closed", false},
      {"endedByClose", false},
  };
  EXPECT_EQ(nlohmann::json::parse(serializeTable(Played())), expected);
  // A race in play writes no ending.
  EXPECT_FALSE(nlohmann::json::parse(serializeTable(*Table::open({"a", "b"}, kPuzzle, kT0, 1)))
                   .contains("ending"));
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
  j = nlohmann::json::parse(serializeTable(Played()));
  j["ending"] = "nap";
  EXPECT_FALSE(deserializeTable(j.dump()).ok());
  j = nlohmann::json::parse(serializeTable(Played()));
  j["puzzle"]["optimal"] = 4294967301LL;  // wraps to 5 as an int
  EXPECT_FALSE(deserializeTable(j.dump()).ok());
}

}  // namespace
}  // namespace wordchain_race
