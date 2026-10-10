#include "domains/games/apis/games_hub/round_robin.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace games_hub {
namespace {

using ::testing::ElementsAre;
using ::testing::FieldsAre;

std::vector<std::string> Entrants(std::size_t n) {
  std::vector<std::string> names;
  for (std::size_t i = 0; i < n; ++i) names.push_back(std::string(1, static_cast<char>('A' + i)));
  return names;
}

std::vector<Pairing> Paired(const std::vector<std::string>& entrants) {
  auto pairings = RoundRobinPairings(entrants);
  EXPECT_TRUE(pairings.ok()) << pairings.status();
  return pairings.ok() ? *pairings : std::vector<Pairing>{};
}

// Records `first`'s score against `second` (1, ½ or 0) on their pairing,
// whichever colour `first` has.
void Score(std::vector<Pairing>& pairings, const std::string& first, const std::string& second,
           double first_scored, bool forfeit = false) {
  for (Pairing& p : pairings) {
    if (p.white == first && p.black == second) {
      p.result = first_scored == 1     ? PairingResult::kWhite
                 : first_scored == 0.5 ? PairingResult::kDraw
                                       : PairingResult::kBlack;
    } else if (p.white == second && p.black == first) {
      p.result = first_scored == 1     ? PairingResult::kBlack
                 : first_scored == 0.5 ? PairingResult::kDraw
                                       : PairingResult::kWhite;
    } else {
      continue;
    }
    p.forfeit = forfeit;
    return;
  }
  ADD_FAILURE() << first << " and " << second << " are not paired";
}

std::vector<std::pair<std::string, int>> Places(const std::vector<Standing>& standings) {
  std::vector<std::pair<std::string, int>> places;
  for (const Standing& s : standings) places.emplace_back(s.player, s.place);
  return places;
}

TEST(RoundRobin, RefusesTooFewTooManyBlankAndRepeatedNames) {
  EXPECT_FALSE(RoundRobinPairings(Entrants(kMinEventEntrants - 1)).ok());
  EXPECT_FALSE(RoundRobinPairings(Entrants(kMaxEventEntrants + 1)).ok());
  EXPECT_FALSE(RoundRobinPairings({"A", "B", "A"}).ok());
  EXPECT_FALSE(RoundRobinPairings({"A", "B", ""}).ok());
  EXPECT_TRUE(RoundRobinPairings(Entrants(kMinEventEntrants)).ok());
  EXPECT_TRUE(RoundRobinPairings(Entrants(kMaxEventEntrants)).ok());
}

TEST(RoundRobin, EightEntrantsIsTwentyEightGames) {
  EXPECT_EQ(Paired(Entrants(kMaxEventEntrants)).size(), 28u);
}

// The pairings are a function of entry order, so every instance computes
// the same event from the same entrants.
TEST(RoundRobin, FourEntrantsPairInBergerOrder) {
  EXPECT_THAT(Paired({"D", "B", "A", "C"}),
              ElementsAre(FieldsAre(1, "D", "C", std::nullopt, false),
                          FieldsAre(1, "B", "A", std::nullopt, false),
                          FieldsAre(2, "C", "B", std::nullopt, false),
                          FieldsAre(2, "A", "D", std::nullopt, false),
                          FieldsAre(3, "A", "C", std::nullopt, false),
                          FieldsAre(3, "D", "B", std::nullopt, false)));
}

TEST(RoundRobin, EveryPairMeetsOnceAndNobodyPlaysTwiceInARound) {
  for (std::size_t n = kMinEventEntrants; n <= kMaxEventEntrants; ++n) {
    SCOPED_TRACE(n);
    const auto entrants = Entrants(n);
    const auto pairings = Paired(entrants);
    const int rounds = static_cast<int>(n % 2 == 0 ? n - 1 : n);

    std::set<std::set<std::string>> met;
    std::map<int, std::set<std::string>> playing;
    for (const Pairing& p : pairings) {
      ASSERT_NE(p.white, p.black);
      EXPECT_TRUE(met.insert({p.white, p.black}).second) << p.white << "-" << p.black;
      ASSERT_GE(p.round, 1);
      ASSERT_LE(p.round, rounds);
      EXPECT_TRUE(playing[p.round].insert(p.white).second) << p.white << " in " << p.round;
      EXPECT_TRUE(playing[p.round].insert(p.black).second) << p.black << " in " << p.round;
    }
    EXPECT_EQ(met.size(), n * (n - 1) / 2);
    EXPECT_TRUE(
        std::is_sorted(pairings.begin(), pairings.end(),
                       [](const Pairing& a, const Pairing& b) { return a.round < b.round; }));
    // An odd field sits one player out a round, and each player once.
    std::map<std::string, int> byes;
    for (int r = 1; r <= rounds; ++r) {
      EXPECT_EQ(playing[r].size(), n - n % 2) << "round " << r;
      for (const auto& name : entrants) byes[name] += playing[r].count(name) ? 0 : 1;
    }
    for (const auto& name : entrants) EXPECT_EQ(byes[name], static_cast<int>(n % 2)) << name;
  }
}

TEST(RoundRobin, ColoursBalanceAndRepeatAtMostOnce) {
  for (std::size_t n = kMinEventEntrants; n <= kMaxEventEntrants; ++n) {
    SCOPED_TRACE(n);
    const auto pairings = Paired(Entrants(n));
    for (const auto& name : Entrants(n)) {
      std::string colours;
      for (const Pairing& p : pairings) {
        if (p.white == name) colours += 'W';
        if (p.black == name) colours += 'B';
      }
      EXPECT_EQ(colours.size(), n - 1) << name;
      const auto whites = std::count(colours.begin(), colours.end(), 'W');
      const auto blacks = std::count(colours.begin(), colours.end(), 'B');
      EXPECT_LE(std::abs(whites - blacks), 1) << name << " " << colours;
      // Alternating as far as the rounds allow: one repeat at most.
      int repeats = 0;
      for (std::size_t i = 1; i < colours.size(); ++i) repeats += colours[i] == colours[i - 1];
      EXPECT_LE(repeats, 1) << name << " " << colours;
    }
  }
}

TEST(RoundRobin, AWinIsOneADrawHalfAndAnUnplayedGameNothing) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  Score(pairings, "C", "D", 0.5);
  Score(pairings, "B", "D", 1, /*forfeit=*/true);

  std::map<std::string, double> points;
  for (const Standing& s : Standings(entrants, pairings, {})) points[s.player] = s.points;
  EXPECT_EQ(points["A"], 1);
  EXPECT_EQ(points["B"], 1);
  EXPECT_EQ(points["C"], 0.5);
  EXPECT_EQ(points["D"], 0.5);
}

// B's forfeit win over D scores D's points, as a played win would.
TEST(RoundRobin, AForfeitCountsTowardSonnebornBerger) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "C", "D", 0.5);
  Score(pairings, "B", "D", 1, /*forfeit=*/true);

  std::map<std::string, double> sb;
  for (const Standing& s : Standings(entrants, pairings, {})) sb[s.player] = s.sonneborn_berger;
  EXPECT_EQ(sb["B"], 0.5);
  EXPECT_EQ(sb["D"], 0.25);
}

// A and B finish level on points. A beat B, but B beat the stronger field
// — and Sonneborn-Berger comes before head-to-head.
TEST(RoundRobin, SonnebornBergerBreaksATieBeforeHeadToHead) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  Score(pairings, "A", "C", 0.5);
  Score(pairings, "A", "D", 0);
  Score(pairings, "B", "C", 0.5);
  Score(pairings, "B", "D", 1);
  Score(pairings, "C", "D", 0);

  const auto standings = Standings(entrants, pairings, {});
  ASSERT_EQ(standings.size(), 4u);
  EXPECT_THAT(Places(standings), ElementsAre(std::pair{"D", 1}, std::pair{"B", 2},
                                             std::pair{"A", 3}, std::pair{"C", 4}));
  EXPECT_EQ(standings[1].sonneborn_berger, 2.5);
  EXPECT_EQ(standings[2].sonneborn_berger, 2);
}

TEST(RoundRobin, HeadToHeadBreaksATieSonnebornBergerLeaves) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 0);
  Score(pairings, "A", "C", 1);
  Score(pairings, "A", "D", 0.5);
  Score(pairings, "B", "C", 0.5);
  Score(pairings, "B", "D", 0);
  Score(pairings, "C", "D", 0.5);

  const auto standings = Standings(entrants, pairings, {});
  ASSERT_EQ(standings.size(), 4u);
  EXPECT_EQ(standings[1].sonneborn_berger, standings[2].sonneborn_berger);
  EXPECT_THAT(Places(standings), ElementsAre(std::pair{"D", 1}, std::pair{"B", 2},
                                             std::pair{"A", 3}, std::pair{"C", 4}));
}

// A, B and C finish on 2. C's Sonneborn-Berger puts them above both, even
// though A's head-to-head win then splits A from B.
TEST(RoundRobin, HeadToHeadOnlySplitsPlayersLevelOnSonnebornBerger) {
  const auto entrants = Entrants(5);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  Score(pairings, "A", "C", 0.5);
  Score(pairings, "A", "D", 0.5);
  Score(pairings, "A", "E", 0);
  Score(pairings, "B", "C", 0.5);
  Score(pairings, "B", "D", 1);
  Score(pairings, "B", "E", 0.5);
  Score(pairings, "C", "D", 0.5);
  Score(pairings, "C", "E", 0.5);
  Score(pairings, "D", "E", 0.5);

  EXPECT_THAT(Places(Standings(entrants, pairings, {})),
              ElementsAre(std::pair{"E", 1}, std::pair{"C", 2}, std::pair{"A", 3},
                          std::pair{"B", 4}, std::pair{"D", 5}));
}

// A, B and C finish on 2½, C behind on Sonneborn-Berger. A beat B, and
// C's games against them — a loss to B, a win over A — don't count.
TEST(RoundRobin, HeadToHeadCountsOnlyGamesAmongThePlayersStillLevel) {
  const auto entrants = Entrants(5);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  Score(pairings, "A", "C", 0);
  Score(pairings, "A", "D", 1);
  Score(pairings, "A", "E", 0.5);
  Score(pairings, "B", "C", 1);
  Score(pairings, "B", "D", 1);
  Score(pairings, "B", "E", 0.5);
  Score(pairings, "C", "D", 0.5);
  Score(pairings, "C", "E", 1);
  Score(pairings, "D", "E", 1);

  EXPECT_THAT(Places(Standings(entrants, pairings, {})),
              ElementsAre(std::pair{"A", 1}, std::pair{"B", 2}, std::pair{"C", 3},
                          std::pair{"D", 4}, std::pair{"E", 5}));
}

TEST(RoundRobin, PlayersLevelOnEverythingShareAPlaceAndTheNextSkips) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 0.5);
  Score(pairings, "A", "C", 0.5);
  Score(pairings, "A", "D", 0.5);
  Score(pairings, "B", "C", 0.5);
  Score(pairings, "B", "D", 0.5);
  Score(pairings, "C", "D", 1);

  EXPECT_THAT(
      Places(Standings(entrants, pairings, {})),
      ElementsAre(std::pair{"C", 1}, std::pair{"A", 2}, std::pair{"B", 2}, std::pair{"D", 4}));
}

// Before a game is played everyone is level: one shared first place, in
// entry order.
TEST(RoundRobin, AnUnplayedEventIsEveryoneSharingFirst) {
  const std::vector<std::string> entrants = {"C", "A", "B"};
  EXPECT_THAT(Places(Standings(entrants, Paired(entrants), {})),
              ElementsAre(std::pair{"C", 1}, std::pair{"A", 1}, std::pair{"B", 1}));
}

// All four level on 1½ and Sonneborn-Berger: their games against each
// other, draws included, are every game, so nobody is split.
TEST(RoundRobin, HeadToHeadCountsDrawsAmongThePlayersStillLevel) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  Score(pairings, "A", "C", 0.5);
  Score(pairings, "A", "D", 0);
  Score(pairings, "B", "C", 0.5);
  Score(pairings, "B", "D", 1);
  Score(pairings, "C", "D", 0.5);

  EXPECT_THAT(
      Places(Standings(entrants, pairings, {})),
      ElementsAre(std::pair{"A", 1}, std::pair{"B", 1}, std::pair{"C", 1}, std::pair{"D", 1}));
}

TEST(RoundRobin, AWithdrawalVoidsUnplayedPairingsAndKeepsPlayedOnes) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  Score(pairings, "A", "B", 1);
  const std::set<std::string> withdrawn = {"A"};

  for (const Pairing& p : pairings) {
    const bool with_a = p.white == "A" || p.black == "A";
    const bool played = p.result.has_value();
    EXPECT_EQ(Voided(p, withdrawn), with_a && !played) << p.white << "-" << p.black;
    EXPECT_FALSE(Voided(p, {}));
  }

  const auto standings = Standings(entrants, pairings, withdrawn);
  ASSERT_EQ(standings.size(), 4u);
  EXPECT_EQ(standings[0].player, "A");
  EXPECT_EQ(standings[0].points, 1);
  EXPECT_TRUE(standings[0].withdrawn);
  for (std::size_t i = 1; i < standings.size(); ++i) EXPECT_FALSE(standings[i].withdrawn);
}

// A tagged challenge names its two players, in either order, and plays
// whenever they're both free: rounds don't gate it.
TEST(RoundRobin, OpenPairingIsFoundByItsPlayersEitherWayRound) {
  const auto entrants = Entrants(4);
  auto pairings = Paired(entrants);
  // A meets B in the last round, as White.
  EXPECT_EQ(OpenPairing(pairings, {}, "A", "B"), std::optional<std::size_t>(5));
  EXPECT_EQ(OpenPairing(pairings, {}, "B", "A"), std::optional<std::size_t>(5));
  EXPECT_EQ(OpenPairing(pairings, {}, "A", "A"), std::nullopt);
  EXPECT_EQ(OpenPairing(pairings, {}, "A", "nobody"), std::nullopt);
  // Voided: either player withdrawn.
  EXPECT_EQ(OpenPairing(pairings, {"B"}, "A", "B"), std::nullopt);
  // Played: nothing left to open.
  Score(pairings, "A", "B", 0.5);
  EXPECT_EQ(OpenPairing(pairings, {}, "A", "B"), std::nullopt);
  EXPECT_EQ(OpenPairing(pairings, {}, "C", "D"), std::optional<std::size_t>(4));
}

}  // namespace
}  // namespace games_hub
