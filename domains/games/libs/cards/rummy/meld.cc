#include "domains/games/libs/cards/rummy/meld.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/card_mapper.h"

namespace rummy {
namespace {

// The ace's rank value below the two: the enum puts it above the king.
constexpr int kLowAce = static_cast<int>(Rank::Two) - 1;

int rankValue(const Card& card, bool aceLow) {
  return aceLow && card.getRank() == Rank::Ace ? kLowAce : static_cast<int>(card.getRank());
}

bool allDistinct(const std::vector<Card>& cards) {
  for (std::size_t i = 0; i < cards.size(); i++) {
    for (std::size_t j = i + 1; j < cards.size(); j++) {
      if (cards[i] == cards[j]) return false;
    }
  }
  return true;
}

// The cards low to high with the ace at the given end, if that is a run.
std::optional<std::vector<Card>> asRun(std::vector<Card> cards, bool aceLow) {
  std::sort(cards.begin(), cards.end(), [aceLow](const Card& a, const Card& b) {
    return rankValue(a, aceLow) < rankValue(b, aceLow);
  });
  for (std::size_t i = 1; i < cards.size(); i++) {
    if (rankValue(cards[i], aceLow) != rankValue(cards[i - 1], aceLow) + 1) return std::nullopt;
  }
  return cards;
}

}  // namespace

std::optional<std::vector<Card>> arrangedMeld(const std::vector<Card>& cards) {
  if (cards.size() < 3 || !allDistinct(cards)) return std::nullopt;
  const Card& first = cards.front();

  const bool oneRank = std::all_of(cards.begin(), cards.end(), [&](const Card& card) {
    return card.getRank() == first.getRank();
  });
  if (oneRank) {
    // Distinct cards of one rank: at most the four suits.
    std::vector<Card> set = cards;
    std::sort(set.begin(), set.end(),
              [](const Card& a, const Card& b) { return a.getSuit() < b.getSuit(); });
    return set;
  }

  const bool oneSuit = std::all_of(cards.begin(), cards.end(), [&](const Card& card) {
    return card.getSuit() == first.getSuit();
  });
  if (!oneSuit) return std::nullopt;
  if (auto high = asRun(cards, /*aceLow=*/false); high.has_value()) return high;
  return asRun(cards, /*aceLow=*/true);
}

std::string faceOf(const Card& card) {
  static constexpr const char* kSuits[] = {"♣", "♦", "♥", "♠"};
  return CardMapper::rankToString(card.getRank()) + kSuits[static_cast<int>(card.getSuit())];
}

bool playable(const Card& card, const std::vector<Card>& pool,
              const std::vector<std::vector<Card>>& table) {
  // A set of the card's rank.
  const auto ofRank = std::count_if(pool.begin(), pool.end(), [&](const Card& other) {
    return other.getRank() == card.getRank();
  });
  if (ofRank >= 3) return true;
  // A run through it: the unbroken stretch of its suit around it, the ace
  // at one end or the other.
  for (const bool aceLow : {false, true}) {
    const auto held = [&](int value) {
      return std::any_of(pool.begin(), pool.end(), [&](const Card& other) {
        return other.getSuit() == card.getSuit() && rankValue(other, aceLow) == value;
      });
    };
    const int at = rankValue(card, aceLow);
    int low = at;
    int high = at;
    while (held(low - 1)) low--;
    while (held(high + 1)) high++;
    if (high - low >= 2) return true;
  }
  // Onto a table meld, growing it with other pool cards until the card
  // fits or nothing more does. A card that grows a meld never stops the
  // card fitting later, except by filling a set, and a set the card fits
  // takes it before anything else is tried.
  for (const std::vector<Card>& meld : table) {
    std::vector<Card> grown = meld;
    for (;;) {
      std::vector<Card> with = grown;
      with.push_back(card);
      if (arrangedMeld(with).has_value()) return true;
      bool added = false;
      for (const Card& other : pool) {
        if (other == card || std::find(grown.begin(), grown.end(), other) != grown.end()) continue;
        std::vector<Card> bigger = grown;
        bigger.push_back(other);
        if (auto arranged = arrangedMeld(bigger); arranged.has_value()) {
          grown = *std::move(arranged);
          added = true;
          break;
        }
      }
      if (!added) break;
    }
  }
  return false;
}

int cardPoints(const Card& card) {
  switch (card.getRank()) {
    case Rank::Ace:
      return 1;
    case Rank::Jack:
    case Rank::Queen:
    case Rank::King:
      return 10;
    default:
      return static_cast<int>(card.getRank()) + 2;
  }
}

}  // namespace rummy
