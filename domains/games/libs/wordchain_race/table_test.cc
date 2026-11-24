#include "domains/games/libs/wordchain_race/table.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace wordchain_race {
namespace {

using Words = std::vector<std::string>;

// From cold to warm: on par in 4 words, or 5 or 6 the long way round.
const Dictionary& Dict() {
  static const Dictionary d =
      Dictionary::FromLines("cold\ncord\ncard\nward\nwart\nwarm\nbold\nbord\nworm\nwore\n");
  return d;
}

const Puzzle kPuzzle{"cold", "warm", 5, "test", {}};
constexpr int64_t kT0 = 1'000'000;
constexpr int64_t kMinute = 60'000;

const Words kPar{"cord", "card", "ward", "warm"};
const Words kMid{"cord", "card", "ward", "wart", "warm"};
const Words kLong{"bold", "bord", "cord", "card", "ward", "warm"};

Race Open(Words players = {"ann", "bob"}) {
  return *Race::open(std::move(players), kPuzzle, kT0, 3 * kMinute);
}

Race Play(Race race, int seat, const Words& words, int64_t at = kT0 + 1000) {
  for (const std::string& word : words) {
    auto next = race.play(seat, word, Dict(), at);
    EXPECT_TRUE(next.ok()) << word << ": " << next.status();
    if (!next.ok()) return race;
    race = *next;
  }
  return race;
}

TEST(RaceOpen, NeedsOneToFourDistinctRunnersAndATime) {
  EXPECT_FALSE(Race::open({}, kPuzzle, kT0, kMinute).ok());
  EXPECT_TRUE(Race::open({"ann"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"a", "b", "c", "d", "e"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"ann", "ann"}, kPuzzle, kT0, kMinute).ok());
  EXPECT_FALSE(Race::open({"ann", "bob"}, kPuzzle, kT0, 0).ok());
  Race race = Open();
  EXPECT_EQ(race.deadlineMs(), kT0 + 3 * kMinute);
  EXPECT_FALSE(race.isOver());
  EXPECT_EQ(race.puzzle().lexicon, "test");
}

TEST(RaceOpen, TakesAPuzzleWithItsLadderOrWithout) {
  Puzzle with = kPuzzle;
  with.ladder = {"cold", "cord", "card", "ward", "warm"};
  EXPECT_TRUE(Race::open({"ann"}, with, kT0, kMinute).ok());
  EXPECT_TRUE(Race::open({"ann"}, kPuzzle, kT0, kMinute).ok());
}

TEST(RaceOpen, RefusesALadderThatIsNotThePuzzles) {
  for (const Words& ladder : {Words{"cold", "bold", "bord", "cord", "card", "ward", "warm"},  // a chain, past par
                              Words{"cord", "card", "ward", "wart", "warm"},  // not from the start
                              Words{"cold", "cord", "card", "ward", "wart"},  // not to the target
                              Words{"cold", "card", "cord", "ward", "warm"}}) {  // a broken rung
    Puzzle puzzle = kPuzzle;
    puzzle.ladder = ladder;
    EXPECT_FALSE(Race::open({"ann"}, puzzle, kT0, kMinute).ok()) << ladder[1];
  }
}

TEST(RacePlay, AcceptsADictionaryWordOneLetterFromTheLast) {
  Race race = Play(Open(), 0, {"cord", "card"});
  EXPECT_EQ(race.runners()[0].ladder, (Words{"cord", "card"}));
  EXPECT_TRUE(race.runners()[1].ladder.empty());
}

TEST(RacePlay, RefusesAWordNotInTheDictionary) {
  EXPECT_EQ(Open().play(0, "colt", Dict(), kT0).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(RacePlay, RefusesAWordNotOneLetterFromTheLast) {
  EXPECT_FALSE(Open().play(0, "card", Dict(), kT0).ok());
  EXPECT_FALSE(Open().play(0, "cold", Dict(), kT0).ok());
}

TEST(RacePlay, RefusesAWordTheLadderAlreadyHolds) {
  Race race = Play(Open(), 0, {"bold", "bord", "cord"});
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
  Race race = Play(Open(), 0, kMid, kT0 + 5000);
  EXPECT_EQ(race.runners()[0].finished_ms, kT0 + 5000);
  EXPECT_FALSE(race.isOver());
  EXPECT_FALSE(race.play(0, "worm", Dict(), kT0 + 6000).ok());
  EXPECT_FALSE(race.retract(0, kT0 + 6000).ok());
}

TEST(RacePlay, RefusesOnceTheClockHasRun) {
  EXPECT_FALSE(Open().play(0, "cord", Dict(), kT0 + 3 * kMinute).ok());
}

TEST(RaceRetract, TakesBackTheLastWordOnly) {
  Race race = *Play(Open(), 0, {"cord", "card"}).retract(0, kT0);
  EXPECT_EQ(race.runners()[0].ladder, (Words{"cord"}));
  EXPECT_FALSE(Open().retract(0, kT0).ok());
}

TEST(RaceEnd, AFinishOnParEndsTheRaceAtOnce) {
  Race race = Play(Open({"ann", "bob", "cy"}), 1, kPar);
  EXPECT_TRUE(race.isOver());
  EXPECT_EQ(race.ending(), Ending::kFinished);
  EXPECT_EQ(race.winners(), (Words{"bob"}));
  EXPECT_FALSE(race.play(0, "cord", Dict(), kT0 + 2000).ok());
}

TEST(RaceEnd, EndsWhenEveryoneFinishesFewestWordsWins) {
  Race race = Play(Open(), 0, kLong, kT0 + 1000);
  EXPECT_FALSE(race.isOver());
  race = Play(race, 1, kMid, kT0 + 9000);
  EXPECT_EQ(race.ending(), Ending::kFinished);
  EXPECT_EQ(race.winners(), (Words{"bob"}));
  EXPECT_EQ(race.standings(), (Words{"bob", "ann"}));
}

TEST(RaceEnd, EarlierFinishBreaksATie) {
  Race race = Play(Open(), 1, kMid, kT0 + 1000);
  race = Play(race, 0, kMid, kT0 + 2000);
  EXPECT_EQ(race.winners(), (Words{"bob"}));
}

TEST(RaceEnd, TheSameWordsAtTheSameMomentIsASharedWin) {
  Race race = Play(Open({"ann", "bob", "cy"}), 1, kMid, kT0 + 1000);
  race = Play(race, 0, kMid, kT0 + 1000);
  race = *race.leave(2);
  EXPECT_EQ(race.winners(), (Words{"ann", "bob"}));
  EXPECT_EQ(ScoreOf(race), (RaceScore{{"ann", "bob"}, Ending::kFinished}));
}

TEST(RaceEnd, TheSameWordsAMillisecondApartIsNot) {
  Race race = Play(Open({"ann", "bob", "cy"}), 1, kMid, kT0 + 1000);
  race = Play(race, 0, kMid, kT0 + 1001);
  race = *race.leave(2);
  EXPECT_EQ(race.winners(), (Words{"bob"}));
}

TEST(RaceEnd, ExpireRefusesBeforeTheDeadlineOrOnceOver) {
  EXPECT_FALSE(Open().expire(kT0 + 3 * kMinute - 1).ok());
  Race over = Play(Open(), 0, kPar);
  EXPECT_FALSE(over.timeUp(kT0 + 4 * kMinute));
  EXPECT_FALSE(over.expire(kT0 + 4 * kMinute).ok());
}

TEST(RaceEnd, OnTheClockFinishersBeatTheRestAndNobodyWinsIfNoneFinished) {
  Race race = Play(Open({"ann", "bob", "cy"}), 2, kMid);
  race = Play(race, 0, {"cord"});
  EXPECT_TRUE(race.timeUp(kT0 + 3 * kMinute));
  race = *race.expire(kT0 + 3 * kMinute);
  EXPECT_EQ(race.ending(), Ending::kTimeout);
  EXPECT_FALSE(race.timeUp(kT0 + 4 * kMinute));
  EXPECT_EQ(race.winners(), (Words{"cy"}));
  EXPECT_EQ(race.standings(), (Words{"cy", "ann", "bob"}));
  EXPECT_TRUE(Open().expire(kT0 + 3 * kMinute)->winners().empty());
}

TEST(RaceLeave, SeatsCountOnlyTheRunnersStillSeated) {
  Race race = *Open({"ann", "bob", "cy"}).leave(1);
  EXPECT_EQ(race.players(), (Words{"ann", "cy"}));
  EXPECT_EQ(race.playerIndex("cy"), 1);
  EXPECT_EQ(race.playerIndex("bob"), -1);
  race = Play(race, 1, {"cord"});
  ASSERT_EQ(race.runners().size(), 3U);
  EXPECT_EQ(race.runners()[2].ladder, (Words{"cord"}));
  EXPECT_TRUE(race.runners()[1].left);
  EXPECT_FALSE(race.leave(2).ok());
}

TEST(RaceLeave, ALeaverKeepsTheirFinishAndCanStillWin) {
  Race race = Play(Open({"ann", "bob", "cy"}), 0, kMid, kT0 + 1000);
  race = *race.leave(0);
  race = Play(race, 0, kLong, kT0 + 2000);
  race = Play(race, 1, kLong, kT0 + 3000);
  EXPECT_EQ(race.ending(), Ending::kFinished);
  EXPECT_EQ(race.winners(), (Words{"ann"}));
}

TEST(RaceLeave, TheLastUnfinishedRunnerLeavingEndsTheRace) {
  Race race = Play(Open({"ann", "bob", "cy"}), 0, kMid);
  race = Play(race, 2, kLong);
  race = *race.leave(1);
  EXPECT_EQ(race.ending(), Ending::kFinished);
  EXPECT_EQ(race.winners(), (Words{"ann"}));
}

TEST(RaceRestore, RefusesALadderThatIsNotAChainOrMisstatesItsFinish) {
  Race race = Play(Open(), 0, kMid, kT0 + 5000);
  auto runners = race.runners();
  EXPECT_TRUE(Race::restore(kPuzzle, runners, kT0, race.deadlineMs(), std::nullopt).ok());

  auto broken = runners;
  broken[0].ladder = {"card"};
  broken[0].finished_ms.reset();
  EXPECT_FALSE(Race::restore(kPuzzle, broken, kT0, race.deadlineMs(), std::nullopt).ok());

  auto unfinished = runners;
  unfinished[0].finished_ms.reset();
  EXPECT_FALSE(Race::restore(kPuzzle, unfinished, kT0, race.deadlineMs(), std::nullopt).ok());

  auto late = runners;
  late[0].finished_ms = race.deadlineMs() + 1;
  EXPECT_FALSE(Race::restore(kPuzzle, late, kT0, race.deadlineMs(), std::nullopt).ok());
}

TEST(RaceRestore, RefusesAnEndingPlayWouldHaveReachedOrMissed) {
  Race par = Play(Open(), 0, kPar);
  EXPECT_TRUE(Race::restore(kPuzzle, par.runners(), kT0, par.deadlineMs(), Ending::kFinished).ok());
  EXPECT_FALSE(Race::restore(kPuzzle, par.runners(), kT0, par.deadlineMs(), std::nullopt).ok());
  EXPECT_FALSE(Race::restore(kPuzzle, Open().runners(), kT0, par.deadlineMs(), Ending::kFinished)
                   .ok());
  EXPECT_TRUE(Race::restore(kPuzzle, Open().runners(), kT0, par.deadlineMs(), Ending::kTimeout)
                  .ok());
}

TEST(RaceRestore, AcceptsAFinishStampedBeforeTheStartOnAnotherClock) {
  Race race = Play(Open(), 0, kMid, kT0 - 5);
  EXPECT_TRUE(
      Race::restore(kPuzzle, race.runners(), kT0, race.deadlineMs(), std::nullopt).ok());
}

Table OpenTable(Words players = {"ann", "bob"}) {
  return *Table::open(std::move(players), kPuzzle, kT0, 3 * kMinute);
}

Table InRace(const Table& table, int seat, const Words& words) {
  auto next = table.inRace([&](const Race& race) { return Play(race, seat, words); });
  EXPECT_TRUE(next.ok()) << next.status();
  return next.ok() ? *next : table;
}

using Sheet = std::vector<RaceScore>;

TEST(Table, ScoresEachRaceAndStartsTheNextOnlyBetweenRaces) {
  Table table = OpenTable();
  EXPECT_FALSE(table.next(kPuzzle, kT0, kMinute).ok());
  table = InRace(table, 0, kMid);
  EXPECT_TRUE(table.scoreSheet().empty());
  table = InRace(table, 1, kLong);
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{"ann"}, Ending::kFinished}}));
  EXPECT_FALSE(table.inRace([](const Race& race) { return race; }).ok());

  table = *table.next(kPuzzle, kT0 + kMinute, kMinute);
  EXPECT_FALSE(table.race().isOver());
  table = *table.inRace([](const Race& race) { return race.expire(kT0 + 2 * kMinute); });
  EXPECT_EQ(table.scoreSheet(),
            (Sheet{{{"ann"}, Ending::kFinished}, {{}, Ending::kTimeout}}));
}

TEST(Table, TheNextRaceSeatsOnlyThoseStillSeated) {
  Table table = InRace(OpenTable({"ann", "bob", "cy"}), 0, kPar);
  table = *table.removePlayer(1);
  table = *table.next(kPuzzle, kT0 + kMinute, kMinute);
  EXPECT_EQ(table.players(), (Words{"ann", "cy"}));
  EXPECT_EQ(table.race().runners().size(), 2U);
}

TEST(Table, ALeaveKeepsTheRaceWhileTwoRemain) {
  Table table = *OpenTable({"ann", "bob", "cy"}).removePlayer(1);
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.players(), (Words{"ann", "cy"}));
  EXPECT_FALSE(table.race().isOver());
}

TEST(Table, ALeaveThatEndsTheRaceScoresIt) {
  Table table = InRace(InRace(OpenTable({"ann", "bob", "cy"}), 0, kMid), 2, kLong);
  table = *table.removePlayer(1);
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{"ann"}, Ending::kFinished}}));
}

TEST(Table, ALeaveDownToOneCreditsAFinisherWhoStayed) {
  Table table = *InRace(OpenTable(), 0, kMid).removePlayer(1);
  EXPECT_TRUE(table.isOver());
  EXPECT_TRUE(table.endedByClose());
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{"ann"}, Ending::kFinished}}));
}

TEST(Table, ALeaveDownToOneWithNobodyFinishedEndsByLeave) {
  Table table = *InRace(OpenTable(), 0, {"cord"}).removePlayer(1);
  EXPECT_TRUE(table.isOver());
  EXPECT_EQ(table.race().ending(), Ending::kLeave);
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{}, Ending::kLeave}}));
  EXPECT_FALSE(table.removePlayer(0).ok());
}

TEST(Table, AClosedTableSaysWhetherTheCloseEndedARace) {
  EXPECT_TRUE(OpenTable().removePlayer(1)->endedByClose());
  Table between = *InRace(OpenTable(), 0, kPar).removePlayer(1);
  EXPECT_TRUE(between.isOver());
  EXPECT_FALSE(between.endedByClose());
  EXPECT_FALSE(OpenTable({"ann", "bob", "cy"}).removePlayer(1)->endedByClose());
}

TEST(Table, AFinishedRaceKeepsItsWinnerWhenTheyLeave) {
  Table table = InRace(InRace(OpenTable({"ann", "bob", "cy"}), 0, kMid), 1, kLong);
  table = InRace(table, 2, kLong);
  table = *table.removePlayer(0);
  EXPECT_EQ(table.race().winners(), (Words{"ann"}));
  EXPECT_EQ(table.scoreSheet().size(), 1U);
  EXPECT_TRUE(Table::restore(table.race(), table.scoreSheet(), false, false).ok());

  Table two = *InRace(InRace(OpenTable(), 0, kMid), 1, kLong).removePlayer(0);
  EXPECT_TRUE(two.isOver());
  EXPECT_EQ(two.race().winners(), (Words{"ann"}));
  EXPECT_TRUE(Table::restore(two.race(), two.scoreSheet(), true, false).ok());
}

TEST(TableRestore, RefusesAFinishedRaceTheSheetDoesNotEndWith) {
  Table table = InRace(InRace(OpenTable(), 0, kMid), 1, kMid);
  EXPECT_TRUE(Table::restore(table.race(), table.scoreSheet(), false, false).ok());
  EXPECT_FALSE(Table::restore(table.race(), {}, false, false).ok());
  EXPECT_FALSE(Table::restore(table.race(), {{{"bob"}, Ending::kFinished}}, false, false).ok());
  EXPECT_FALSE(Table::restore(table.race(), {{{"ann"}, Ending::kTimeout}}, false, false).ok());
}

TEST(TableRestore, RefusesSeatsThatDisagreeWithBeingClosed) {
  Table closed = *OpenTable().removePlayer(1);
  EXPECT_TRUE(Table::restore(closed.race(), closed.scoreSheet(), true, true).ok());
  EXPECT_FALSE(Table::restore(closed.race(), closed.scoreSheet(), false, false).ok());
  Table done = InRace(OpenTable(), 0, kPar);
  EXPECT_FALSE(Table::restore(done.race(), done.scoreSheet(), true, false).ok());
  // Only a closed table's close ended a race.
  EXPECT_FALSE(Table::restore(done.race(), done.scoreSheet(), false, true).ok());
}

TEST(Practice, ATableOfOneIsPracticeAndRacesOnAlone) {
  Table table = OpenTable({"ann"});
  EXPECT_TRUE(table.practice());
  EXPECT_FALSE(table.isOver());
  table = InRace(table, 0, kMid);
  EXPECT_TRUE(table.race().isOver());
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{"ann"}, Ending::kFinished}}));
  table = *table.next(kPuzzle, kT0 + kMinute, kMinute);
  EXPECT_TRUE(table.practice());
  EXPECT_FALSE(table.race().isOver());
}

TEST(Practice, ATableOfTwoIsNotEvenOnceOneHasLeft) {
  EXPECT_FALSE(OpenTable().practice());
  Table left = *OpenTable({"ann", "bob", "cy"}).removePlayer(1);
  EXPECT_FALSE(left.practice());
  EXPECT_FALSE(left.removePlayer(0)->practice());
}

TEST(Practice, TheClockEndsAPracticeRace) {
  Table table =
      *OpenTable({"ann"}).inRace([](const Race& race) { return race.expire(kT0 + 3 * kMinute); });
  EXPECT_EQ(table.scoreSheet(), (Sheet{{{}, Ending::kTimeout}}));
  EXPECT_FALSE(table.isOver());
}

TEST(Practice, LeavingClosesThePracticeTable) {
  Table racing = *OpenTable({"ann"}).removePlayer(0);
  EXPECT_TRUE(racing.isOver());
  EXPECT_TRUE(racing.endedByClose());
  EXPECT_TRUE(racing.practice());
  Table between = *InRace(OpenTable({"ann"}), 0, kPar).removePlayer(0);
  EXPECT_TRUE(between.isOver());
  EXPECT_FALSE(between.endedByClose());
}

TEST(TableRestore, APracticeTableIsOpenWithItsOneSeatAndClosedWithout) {
  Table open = InRace(OpenTable({"ann"}), 0, {"cord"});
  EXPECT_TRUE(Table::restore(open.race(), open.scoreSheet(), false, false).ok());
  Table done = InRace(OpenTable({"ann"}), 0, kPar);
  EXPECT_FALSE(Table::restore(done.race(), done.scoreSheet(), true, false).ok());
  Table closed = *OpenTable({"ann"}).removePlayer(0);
  EXPECT_TRUE(Table::restore(closed.race(), closed.scoreSheet(), true, true).ok());
  EXPECT_FALSE(Table::restore(closed.race(), closed.scoreSheet(), false, false).ok());
}

}  // namespace
}  // namespace wordchain_race
