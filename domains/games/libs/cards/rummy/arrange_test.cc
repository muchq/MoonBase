#include "domains/games/libs/cards/rummy/arrange.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/rummy/meld.h"

using namespace cards;
using namespace rummy;
using nlohmann::json;

namespace {

const std::vector<std::string> kRanks = {"2", "3",  "4", "5", "6", "7", "8",
                                         "9", "10", "J", "Q", "K", "A"};
const std::vector<std::string> kSuits = {"♣", "♦", "♥", "♠"};

// "10♥" as the wire spells it.
Card c(const std::string& face) {
  for (std::size_t s = 0; s < kSuits.size(); s++) {
    const std::string& suit = kSuits[s];
    if (face.size() > suit.size() &&
        face.compare(face.size() - suit.size(), suit.size(), suit) == 0) {
      const std::string rank = face.substr(0, face.size() - suit.size());
      const auto at = std::find(kRanks.begin(), kRanks.end(), rank);
      if (at == kRanks.end()) break;
      return Card{static_cast<Suit>(s), static_cast<Rank>(at - kRanks.begin())};
    }
  }
  ADD_FAILURE() << "no such card: " << face;
  return Card{Suit::Clubs, Rank::Two};
}

std::vector<Card> hand(const json& faces) {
  std::vector<Card> cards;
  for (const json& face : faces) cards.push_back(c(face.get<std::string>()));
  return cards;
}

int points(const std::vector<Card>& cards) {
  int sum = 0;
  for (const Card& card : cards) sum += cardPoints(card);
  return sum;
}

// Melds valid and laid as arrangedMeld lays them; melds, deadwood and any
// lay-offs together exactly the hand; the deadwood's cost as stated.
void expectWellFormed(const std::vector<Card>& in, const Arrangement& out,
                      const std::vector<Card>& laidOff = {}) {
  std::vector<Card> seen = out.deadwood;
  seen.insert(seen.end(), laidOff.begin(), laidOff.end());
  for (const auto& meld : out.melds) {
    const auto arranged = arrangedMeld(meld);
    ASSERT_TRUE(arranged.has_value());
    EXPECT_EQ(*arranged, meld);
    seen.insert(seen.end(), meld.begin(), meld.end());
  }
  auto key = [](const Card& a, const Card& b) {
    return std::pair(a.getSuit(), a.getRank()) < std::pair(b.getSuit(), b.getRank());
  };
  std::vector<Card> want = in;
  std::sort(want.begin(), want.end(), key);
  std::sort(seen.begin(), seen.end(), key);
  EXPECT_EQ(seen, want);
  EXPECT_EQ(out.deadwoodPoints, points(out.deadwood));
}

json corpus() {
  std::ifstream file("domains/games/libs/cards/rummy/testdata/arrange_corpus.json");
  EXPECT_TRUE(file.good());
  return json::parse(file);
}

// The oracle: every subset of the hand as melded cards, kept if it splits
// into melds.
bool partitionable(std::vector<Card> cards) {
  if (cards.empty()) return true;
  const Card first = cards.front();
  cards.erase(cards.begin());
  const std::size_t n = cards.size();
  for (unsigned mask = 1; mask < (1u << n); mask++) {
    if (__builtin_popcount(mask) < 2) continue;
    std::vector<Card> group{first};
    std::vector<Card> rest;
    for (std::size_t i = 0; i < n; i++) (mask & (1u << i) ? group : rest).push_back(cards[i]);
    if (arrangedMeld(group).has_value() && partitionable(rest)) return true;
  }
  return false;
}

bool laysOff(const std::vector<Card>& cards, const std::vector<std::vector<Card>>& onto) {
  if (cards.empty()) return true;
  if (onto.empty()) return false;
  std::vector<std::size_t> assign(cards.size(), 0);
  for (;;) {
    std::vector<std::vector<Card>> grown = onto;
    for (std::size_t i = 0; i < cards.size(); i++) grown[assign[i]].push_back(cards[i]);
    if (std::all_of(grown.begin(), grown.end(),
                    [](const auto& meld) { return arrangedMeld(meld).has_value(); })) {
      return true;
    }
    std::size_t i = 0;
    while (i < assign.size() && ++assign[i] == onto.size()) assign[i++] = 0;
    if (i == assign.size()) return false;
  }
}

int oracle(const std::vector<Card>& cards) {
  int best = points(cards);
  const std::size_t n = cards.size();
  for (unsigned mask = 0; mask < (1u << n); mask++) {
    std::vector<Card> melded;
    std::vector<Card> left;
    for (std::size_t i = 0; i < n; i++) (mask & (1u << i) ? melded : left).push_back(cards[i]);
    if (points(left) < best && partitionable(melded)) best = points(left);
  }
  return best;
}

}  // namespace

TEST(Arrange, TheCorpusDeadwoodIsTheLeastThereIs) {
  const json cases = corpus()["arrange"];
  ASSERT_GE(cases.size(), 30u);
  for (const json& entry : cases) {
    const std::vector<Card> in = hand(entry["hand"]);
    const Arrangement out = bestArrangement(in);
    EXPECT_EQ(out.deadwoodPoints, entry["deadwood"].get<int>()) << entry["hand"].dump();
    expectWellFormed(in, out);
  }
}

TEST(Arrange, LayOffsInTheCorpusLeaveTheLeastDeadwood) {
  const json cases = corpus()["layOff"];
  ASSERT_GE(cases.size(), 15u);
  for (const json& entry : cases) {
    const std::vector<Card> in = hand(entry["hand"]);
    std::vector<std::vector<Card>> onto;
    for (const json& meld : entry["onto"]) onto.push_back(hand(meld));
    const LaidOffArrangement out = bestWithLayOffs(in, onto);
    EXPECT_EQ(out.own.deadwoodPoints, entry["deadwood"].get<int>()) << entry["hand"].dump();
    expectWellFormed(in, out.own, out.laidOff);
    // The laid-off cards go onto the knocker's melds, each onto one, and
    // every meld so grown is a meld.
    EXPECT_TRUE(laysOff(out.laidOff, onto)) << entry["hand"].dump();
  }
}

TEST(Arrange, AgreesWithTheOracleOnSeededHands) {
  std::mt19937 rng(1610);
  std::vector<Card> deck;
  for (int i = 0; i < 52; i++) deck.emplace_back(i);
  for (int round = 0; round < 60; round++) {
    std::shuffle(deck.begin(), deck.end(), rng);
    const std::vector<Card> in(deck.begin(), deck.begin() + (round % 2 == 0 ? 10 : 11));
    const Arrangement out = bestArrangement(in);
    EXPECT_EQ(out.deadwoodPoints, oracle(in)) << round;
    expectWellFormed(in, out);
  }
}

TEST(Arrange, AnEmptyHandIsNothing) {
  const Arrangement out = bestArrangement({});
  EXPECT_TRUE(out.melds.empty());
  EXPECT_TRUE(out.deadwood.empty());
  EXPECT_EQ(out.deadwoodPoints, 0);
}

TEST(Arrange, WithNothingToLayOffOntoTheDefenderKeepsItsOwnBest) {
  const std::vector<Card> in = {c("7♥"), c("7♣"), c("7♦"), c("K♠")};
  const LaidOffArrangement out = bestWithLayOffs(in, {});
  EXPECT_TRUE(out.laidOff.empty());
  EXPECT_EQ(out.own.deadwoodPoints, 10);
}
