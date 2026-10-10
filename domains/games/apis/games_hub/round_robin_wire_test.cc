#include "domains/games/apis/games_hub/round_robin_wire.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/round_robin.h"

namespace games_hub {
namespace {

using ::testing::ElementsAre;

HubStore::ChessEventRow Row(std::vector<std::string> entrants) {
  HubStore::ChessEventRow row;
  row.room_id = "R1";
  row.event_id = "E1";
  row.version = 3;
  row.creator = entrants[0];
  row.entrants = std::move(entrants);
  row.terms = {"standard", {300'000, 3'000}};
  row.pairings = *RoundRobinPairings(row.entrants);
  return row;
}

// Records `first`'s score against `second` on their pairing.
void Score(HubStore::ChessEventRow& row, const std::string& first, const std::string& second,
           double first_scored) {
  for (Pairing& p : row.pairings) {
    const bool as_white = p.white == first && p.black == second;
    if (!as_white && !(p.white == second && p.black == first)) continue;
    const double white_scored = as_white ? first_scored : 1 - first_scored;
    p.result = white_scored == 1     ? PairingResult::kWhite
               : white_scored == 0.5 ? PairingResult::kDraw
                                     : PairingResult::kBlack;
    p.forfeit = true;
    return;
  }
  ADD_FAILURE() << first << " and " << second << " are not paired";
}

TEST(RoundRobinWire, TheRowsIdentityEntrantsAndTermsCarryOver) {
  const auto view = RoundRobinOf(Row({"A", "B", "C"}));
  EXPECT_EQ(view.roundRobinId, "E1");
  EXPECT_EQ(view.creator, "A");
  EXPECT_THAT(view.entrants, ElementsAre("A", "B", "C"));
  EXPECT_EQ(view.terms.setupId, "standard");
  EXPECT_EQ(view.terms.setupName, "Standard starting position");
  EXPECT_EQ(view.terms.initialSeconds, 300);
  EXPECT_EQ(view.terms.incrementSeconds, 3);
}

TEST(RoundRobinWire, PairingsCarryTheirResultForfeitAndWhetherVoided) {
  HubStore::ChessEventRow row = Row({"A", "B", "C", "D"});
  Score(row, "A", "D", 1);  // A is white
  Score(row, "C", "A", 0);  // C is white: black wins
  Score(row, "B", "C", 0.5);
  row.pairings[1].forfeit = false;  // B-C: as if played
  row.withdrawn = {"D"};
  const auto view = RoundRobinOf(row);
  ASSERT_EQ(view.pairings.size(), 6u);
  EXPECT_EQ(view.pairings[0].round, 1);
  EXPECT_EQ(view.pairings[0].white, "A");
  EXPECT_EQ(view.pairings[0].black, "D");
  EXPECT_EQ(view.pairings[0].result, "white");
  EXPECT_TRUE(view.pairings[0].forfeit);
  EXPECT_FALSE(view.pairings[0].voided) << "played before D withdrew";
  EXPECT_EQ(view.pairings[1].result, "draw");
  EXPECT_FALSE(view.pairings[1].forfeit);
  EXPECT_FALSE(view.pairings[2].result.has_value());  // D-B
  EXPECT_TRUE(view.pairings[2].voided);
  EXPECT_EQ(view.pairings[3].result, "black");  // C-A
  EXPECT_FALSE(view.pairings[5].voided);        // A-B, still to play
  EXPECT_THAT(view.withdrawn, ElementsAre("D"));
}

// The standings as round_robin.h ranks them: points, Sonneborn-Berger, a
// shared place and the skip after it, and who withdrew.
TEST(RoundRobinWire, StandingsCarryPointsSonnebornBergerPlacesAndWithdrawals) {
  HubStore::ChessEventRow row = Row({"A", "B", "C", "D"});
  Score(row, "A", "B", 0.5);
  Score(row, "A", "C", 0.5);
  Score(row, "A", "D", 0.5);
  Score(row, "B", "C", 0.5);
  Score(row, "B", "D", 0.5);
  Score(row, "C", "D", 1);
  row.withdrawn = {"B"};
  const auto view = RoundRobinOf(row);
  ASSERT_EQ(view.standings.size(), 4u);
  std::vector<std::pair<std::string, int>> places;
  for (const auto& standing : view.standings)
    places.emplace_back(standing.playerId, standing.place);
  EXPECT_THAT(places, ElementsAre(std::pair{"C", 1}, std::pair{"A", 2}, std::pair{"B", 2},
                                  std::pair{"D", 4}));
  EXPECT_EQ(view.standings[0].points, 2);
  EXPECT_EQ(view.standings[0].sonnebornBerger, 2.5);
  EXPECT_EQ(view.standings[1].points, 1.5);
  EXPECT_EQ(view.standings[1].sonnebornBerger, 2.25);
  EXPECT_FALSE(view.standings[1].withdrawn);
  EXPECT_TRUE(view.standings[2].withdrawn);
}

}  // namespace
}  // namespace games_hub
