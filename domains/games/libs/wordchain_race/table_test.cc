#include "domains/games/libs/wordchain_race/table.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace wordchain_race {
namespace {

using Words = std::vector<std::string>;

// cold → cord → card → ward → warm is shortest; cold → bold → bord → cord →
// card → ward → warm climbs it the long way.
const Dictionary& Dict() {
  static const Dictionary d =
      Dictionary::FromLines("cold\ncord\ncard\nward\nwarm\nbold\nbord\nworm\nwore\n");
  return d;
}

const Puzzle kPuzzle{"cold", "warm", 5};
constexpr int64_t kT0 = 1'000'000;
constexpr int64_t kMinute = 60'000;

Race Open(Words players = {"ann", "bob"}) {
  return *Race::open(std::move(players), kPuzzle, kT0, 3 * kMinute);
}

Race Play(Race race, int seat, const Words& words, int64_t at = kT0 + 1000) {
  for (const std::string& word : words) {
    auto next = race.play(seat, word, Dict(), at);
    EXPECT_TRUE(next.ok()) << word << ": " << next.status();
    race = *next;
  }
  return race;
}

const Words kShort{"cord", "card", "ward", "warm"};
const Words kLong{"bold", "bord", "cord", "card", "ward", "warm"};

TEST(RaceOpen, NeedsTwoToFourDistinctRunnersAndATime) {
  EXPECT_FALSE(Race::open({"ann"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"a", "b", "c", "d", "e"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"ann", "ann"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"ann", "bob"}, kPuzzle, kT0, 0).ok());
  Race race = Open();
  EXPECT_EQ(race.deadlineMs(), kT0 + 3 * kMinute);
  EXPECT_FALSE(race.isOver());
}

TEST(RacePlay, AcceptsADictionaryWordOneLetterFromTheLast) {
  Race race = Play(Open(), 0, {"cord", "card"});
  EXPECT_EQ(race.runners()[0].ladder, (Words{"cord", "card"}));
  EXPECT_TRUE(race.runners()[1].ladder.empty());
}

TEST(RacePlay, RefusesAWordNotInTheDictionary) {
  auto next = Open().play(0, "colt", Dict(), kT0);
  EXPECT_EQ(next.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RacePlay, RefusesAWordNotOneLetterFromTheLast) {
  EXPECT_FALSE(Open().play(0, "card", Dict(), kT0).ok());
  EXPECT_FALSE(Open().play(0, "cold", Dict(), kT0).ok());
}

TEST(RacePlay, RefusesAWordTheLadderAlreadyHolds) {
  Race race = Play(Open(), 0, {"bold", "bord", "cord"});
  // cord → cold would return to the start.
  EXPECT_FALSE(race.play(0, "cold", Dict(), kT0).ok());
  race = Play(race, 0, {"card"});
  EXPECT_FALSE(race.play(0, "cord", Dict(), kT0).ok());
}

TEST(RacePlay, NormalizesCaseAndRefusesNonLetters) {
  Race race = Play(Open(), 0, {"CORD"});
  EXPECT_EQ(race.runners()[0].ladder, (Words{"cord"}));
  EXPECT_FALSE(race.play(0, "c0rd", Dict(), kT0).ok());
}

TEST(RacePlay, RefusesASeatOutOfRange) {
  EXPECT_FALSE(Open().play(2, "cord", Dict(), kT0).ok());
  EXPECT_FALSE(Open().play(-1, "cord", Dict(), kT0).ok());
}

TEST(RacePlay, ReachingTheTargetFinishesTheRunnerWhoThenCannotPlay) {
  Race race = Play(Open(), 0, kShort, kT0 + 5000);
  EXPECT_EQ(race.runners()[0].finished_ms, kT0 + 5000);
  EXPECT_FALSE(race.isOver());
  EXPECT_FALSE(race.play(0, "worm", Dict(), kT0 + 6000).ok());
  EXPECT_FALSE(race.retract(0, kT0 + 6000).ok());
}

TEST(RacePlay, RefusesOnceTheClockHasRun) {
  EXPECT_FALSE(Open().play(0, "cord", Dict(), kT0 + 3 * kMinute).ok());
}

TEST(RaceRetract, TakesBackTheLastWordOnly) {
  Race race = Play(Open(), 0, {"cord", "card"});
  race = *race.retract(0, kT0);
  EXPECT_EQ(race.runners()[0].ladder, (Words{"cord"}));
  EXPECT_FALSE(Open().retract(0, kT0).ok());
}

TEST(RaceEnd, EndsWhenEveryoneFinishesFewestWordsWins) {
  Race race = Play(Open(), 0, kLong, kT0 + 1000);
  race = Play(race, 1, kShort, kT0 + 9000);
  EXPECT_TRUE(race.isOver());
  EXPECT_FALSE(race.timedOut());
  EXPECT_EQ(race.winner(), "bob");
  EXPECT_EQ(race.standings(), (Words{"bob", "ann"}));
}

TEST(RaceEnd, EarlierFinishBreaksATie) {
  Race race = Play(Open(), 1, kShort, kT0 + 1000);
  race = Play(race, 0, kShort, kT0 + 2000);
  EXPECT_EQ(race.winner(), "bob");
}

TEST(RaceEnd, OnTheClockFinishersBeatTheRestAndNobodyWinsIfNoneFinished) {
  Race race = Play(Open({"ann", "bob", "cy"}), 2, kShort);
  race = Play(race, 0, {"cord"});
  EXPECT_FALSE(race.timeUp(kT0 + 3 * kMinute - 1));
  EXPECT_TRUE(race.timeUp(kT0 + 3 * kMinute));
  race = race.expire();
  EXPECT_TRUE(race.isOver());
  EXPECT_TRUE(race.timedOut());
  EXPECT_FALSE(race.timeUp(kT0 + 4 * kMinute));
  EXPECT_EQ(race.winner(), "cy");
  EXPECT_EQ(race.standings(), (Words{"cy", "ann", "bob"}));
  EXPECT_EQ(Open().expire().winner(), std::nullopt);
}

TEST(RaceLeave, DroppingTheLastUnfinishedRunnerEndsTheRace) {
  Race race = Play(Open({"ann", "bob", "cy"}), 0, kShort);
  race = *race.withoutRunner(1);
  EXPECT_FALSE(race.isOver());
  race = *race.withoutRunner(1);
  EXPECT_TRUE(race.isOver());
  EXPECT_EQ(race.winner(), "ann");
}

TEST(RaceRestore, RefusesALadderThatIsNotAChainOrMisstatesItsFinish) {
  Race race = Play(Open(), 0, kShort, kT0 + 5000);
  auto runners = race.runners();
  EXPECT_TRUE(Race::restore(kPuzzle, runners, kT0, race.deadlineMs(), false, false).ok());

  auto broken = runners;
  broken[0].ladder = {"card"};
  broken[0].finished_ms.reset();
  EXPECT_FALSE(Race::restore(kPuzzle, broken, kT0, race.deadlineMs(), false, false).ok());

  auto unfinished = runners;
  unfinished[0].finished_ms.reset();
  EXPECT_FALSE(Race::restore(kPuzzle, unfinished, kT0, race.deadlineMs(), false, false).ok());

  auto early = runners;
  early[1].finished_ms = kT0;
  EXPECT_FALSE(Race::restore(kPuzzle, early, kT0, race.deadlineMs(), false, false).ok());
}

Table OpenTable(Words players = {"ann", "bob"}) {
  return *Table::open(std::move(players), kPuzzle, kT0, 3 * kMinute);
}

Table InRace(const Table& table, int seat, const Words& words) {
  auto next = table.inRace([&](const Race& race) { return Play(race, seat, words); });
  EXPECT_TRUE(next.ok()) << next.status();
  return *next;
}

TEST(Table, ScoresEachRaceAndStartsTheNextOnlyBetweenRaces) {
  Table table = OpenTable();
  EXPECT_FALSE(table.next(kPuzzle, kT0, kMinute).ok());
  table = InRace(table, 0, kShort);
  EXPECT_TRUE(table.scoreSheet().empty());
  table = InRace(table, 1, kLong);
  EXPECT_EQ(table.scoreSheet(), (std::vector<RaceScore>{{"ann"}}));
  EXPECT_FALSE(table.inRace([](const Race& race) { return race; }).ok());

  table = *table.next(kPuzzle, kT0 + kMinute, kMinute);
  EXPECT_FALSE(table.race().isOver());
  EXPECT_EQ(table.players(), (Words{"ann", "bob"}));
  table = *table.inRace([](const Race& race) { return race.expire(); });
  EXPECT_EQ(table.scoreSheet(), (std::vector<RaceScore>{{"ann"}, {std::nullopt}}));
}

TEST(Table, ALeaveKeepsTheRaceWhileTwoRemain) {
  Table table = *OpenTable({"ann", "bob", "cy"}).removePlayer(1);
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.players(), (Words{"ann", "cy"}));
  EXPECT_FALSE(table.race().isOver());
}

TEST(Table, ALeaveThatEndsTheRaceScoresIt) {
  Table table = InRace(OpenTable({"ann", "bob", "cy"}), 0, kShort);
  table = InRace(table, 2, kLong);
  table = *table.removePlayer(1);
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.scoreSheet(), (std::vector<RaceScore>{{"ann"}}));
}

TEST(Table, ALeaveDownToOneClosesAndAbandonsTheRaceInPlay) {
  Table table = InRace(OpenTable(), 0, kShort);
  table = *table.removePlayer(1);
  EXPECT_TRUE(table.isOver());
  EXPECT_TRUE(table.race().abandoned());
  EXPECT_EQ(table.race().winner(), std::nullopt);
  EXPECT_EQ(table.scoreSheet(), (std::vector<RaceScore>{{std::nullopt}}));
  EXPECT_FALSE(table.removePlayer(0).ok());
}

TEST(Table, ALeaveDownToOneBetweenRacesScoresNothingMore) {
  Table table = InRace(InRace(OpenTable(), 0, kShort), 1, kShort);
  table = *table.removePlayer(0);
  EXPECT_TRUE(table.isOver());
  EXPECT_EQ(table.scoreSheet().size(), 1U);
}

TEST(TableRestore, RefusesASheetThatDisagreesWithTheRace) {
  Table table = InRace(InRace(OpenTable(), 0, kShort), 1, kShort);
  EXPECT_TRUE(Table::restore(table.race(), table.scoreSheet(), false).ok());
  EXPECT_FALSE(Table::restore(table.race(), {}, false).ok());
  EXPECT_FALSE(Table::restore(OpenTable().race(), {}, true).ok());
  EXPECT_FALSE(Table::restore(table.race(), {{"zed"}}, false).ok());
}

}  // namespace
}  // namespace wordchain_race
