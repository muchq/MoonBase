#include "domains/games/apis/games_hub/game_events.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/castle/player.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/player.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace games_hub {
namespace {

using ::cards::Card;
using ::cards::Rank;
using ::cards::Suit;
using ::testing::HasSubstr;

golf::Player GolfSeat(const std::string& id, Rank rank) {
  return golf::Player{id, Card(Suit::Clubs, rank), Card(Suit::Diamonds, rank),
                      Card(Suit::Hearts, rank), Card(Suit::Spades, rank)};
}

/// Two golf seats mid-hand, `who_knocked` as given, the knocker to move —
/// so a knock has come back around and the game is over.
golf::GameState Golf(int who_knocked) {
  return golf::GameState{std::deque<Card>{Card{Suit::Diamonds, Rank::Ten}},
                         std::deque<Card>{Card{Suit::Hearts, Rank::Four}},
                         {GolfSeat("andy", Rank::Two), GolfSeat("mercy", Rank::Three)},
                         false,
                         0,
                         who_knocked,
                         "game",
                         "v0"};
}

castle::Player CastleSeat(const std::string& id) {
  return castle::Player{id, {Card{Suit::Clubs, Rank::Five}}, {}, {}, true};
}

/// Two castle seats in play, one card each, seat 0 to move.
castle::GameState Castle() {
  return castle::GameState{
      {},     {},  {CastleSeat("andy"), CastleSeat("mercy")}, 0, castle::Phase::Playing, {},
      "game", "v0"};
}

TEST(GameEvents, AGolfGamePlayedOutIsCompleted) {
  const golf::GameState knocked = Golf(0);
  ASSERT_TRUE(knocked.isOver());

  const GameFinished finished = FinishedOf(HostedState(knocked), 2);
  EXPECT_EQ(finished.variant, "golf");
  EXPECT_EQ(finished.outcome, "completed");
  EXPECT_EQ(finished.players, 2u);
}

// Golf's forced finish supersedes any knock with the kAbandoned
// sentinel, which is the only thing that tells the two endings apart:
// the scorecard keeps every seat either way.
TEST(GameEvents, AGolfGameLeftBelowTwoSeatsIsAbandoned) {
  const golf::GameState lone = *Golf(golf::GameState::kNoKnock).removePlayer(1);
  ASSERT_TRUE(lone.isOver());
  ASSERT_EQ(lone.getWhoKnocked(), golf::GameState::kAbandoned);

  const GameFinished finished = FinishedOf(HostedState(lone), 1);
  EXPECT_EQ(finished.variant, "golf");
  EXPECT_EQ(finished.outcome, "abandoned");
  EXPECT_EQ(finished.players, 1u);
}

TEST(GameEvents, ACastleGamePlayedOutIsCompleted) {
  const absl::StatusOr<castle::GameState> over = Castle().playFromHand(0, {0});
  ASSERT_TRUE(over.ok()) << over.status();
  ASSERT_EQ(over->getPhase(), castle::Phase::Over);

  const GameFinished finished = FinishedOf(HostedState(*over), 2);
  EXPECT_EQ(finished.variant, "castle");
  EXPECT_EQ(finished.outcome, "completed");
  EXPECT_EQ(finished.players, 2u);
}

TEST(GameEvents, ACastleGameLeftBelowTwoSeatsIsAbandoned) {
  const absl::StatusOr<castle::GameState> gone = Castle().removePlayer(1);
  ASSERT_TRUE(gone.ok()) << gone.status();
  ASSERT_EQ(gone->getPhase(), castle::Phase::Abandoned);

  const GameFinished finished = FinishedOf(HostedState(*gone), 1);
  EXPECT_EQ(finished.variant, "castle");
  EXPECT_EQ(finished.outcome, "abandoned");
  EXPECT_EQ(finished.players, 1u);
}

TEST(GameEvents, TheLineIsOneJsonObjectNamingTheEventAndWhenItHappened) {
  const absl::Time when =
      absl::FromCivil(absl::CivilSecond(2026, 9, 21, 13, 40, 0), absl::UTCTimeZone());
  EXPECT_EQ(GameFinishedLine(when, GameFinished{"golf", kCompleted, 3}),
            R"({"ts":1789998000000,"event":"game_finished","variant":"golf",)"
            R"("outcome":"completed","players":3})");
}

// Nothing in the line is escaped, and nothing may need to be: a value
// that could carry a quote, a backslash or a newline would make this a
// hand-rolled encoder rather than a format string. Run over every ending
// of every game, so a third variant or a third outcome comes through here.
TEST(GameEvents, EveryEndingsLineIsTextWithNothingToEscape) {
  const absl::Time when = absl::Now();
  std::vector<HostedState> endings;
  endings.emplace_back(Golf(0));
  endings.emplace_back(*Golf(golf::GameState::kNoKnock).removePlayer(1));
  endings.emplace_back(*Castle().playFromHand(0, {0}));
  endings.emplace_back(*Castle().removePlayer(1));

  for (const HostedState& state : endings) {
    for (std::size_t players = 1; players <= 4; ++players) {
      const GameFinished finished = FinishedOf(state, players);
      EXPECT_TRUE(finished.outcome == kCompleted || finished.outcome == kAbandoned)
          << finished.outcome;
      EXPECT_TRUE(finished.variant == "golf" || finished.variant == "castle") << finished.variant;

      const std::string line = GameFinishedLine(when, finished);
      EXPECT_EQ(line.find('\\'), std::string::npos) << line;
      EXPECT_EQ(line.find('\n'), std::string::npos) << line;
      EXPECT_EQ(std::count(line.begin(), line.end(), '"'), 16) << line;
      EXPECT_THAT(line, HasSubstr(R"("event":"game_finished")"));
    }
  }
}

}  // namespace
}  // namespace games_hub
