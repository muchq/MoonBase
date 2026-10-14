#include "domains/games/apis/games_hub/golf_adapter.h"

#include <deque>
#include <string>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/player.h"
#include "gtest/gtest.h"

namespace games_hub {
namespace {

using ::cards::Card;
using ::cards::Rank;
using ::cards::Suit;
using Golf = Adapter<golf::GameState>;

golf::Player GolfSeat(const std::string& id, Rank rank) {
  return golf::Player{id, Card(Suit::Clubs, rank), Card(Suit::Diamonds, rank),
                      Card(Suit::Hearts, rank), Card(Suit::Spades, rank)};
}

/// Two golf seats mid-hand, `who_knocked` as given, the knocker to move —
/// so a knock has come back around and the game is over.
golf::GameState Knocked(int who_knocked) {
  return golf::GameState{std::deque<Card>{Card{Suit::Diamonds, Rank::Ten}},
                         std::deque<Card>{Card{Suit::Hearts, Rank::Four}},
                         {GolfSeat("andy", Rank::Two), GolfSeat("mercy", Rank::Three)},
                         false,
                         0,
                         who_knocked,
                         "game",
                         "v0"};
}

TEST(GolfAdapter, AGamePlayedOutIsCompleted) {
  const golf::GameState knocked = Knocked(0);
  ASSERT_TRUE(knocked.isOver());
  EXPECT_EQ(Golf::Ended(knocked), GameEnd::kCompleted);
}

// Golf's forced finish supersedes any knock with the kAbandoned
// sentinel, which is the only thing that tells the two endings apart:
// the scorecard keeps every seat either way.
TEST(GolfAdapter, AGameLeftBelowTwoSeatsIsAbandoned) {
  const golf::GameState lone = *Knocked(golf::GameState::kNoKnock).removePlayer(1);
  ASSERT_TRUE(lone.isOver());
  ASSERT_EQ(lone.getWhoKnocked(), golf::GameState::kAbandoned);
  EXPECT_EQ(Golf::Ended(lone), GameEnd::kAbandoned);
}

}  // namespace
}  // namespace games_hub
