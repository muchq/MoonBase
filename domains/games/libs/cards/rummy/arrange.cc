#include "domains/games/libs/cards/rummy/arrange.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "domains/games/libs/cards/rummy/meld.h"

namespace rummy {
namespace {

using Mask = std::uint32_t;

int rankValue(const Card& card, bool aceLow) {
  const int rank = static_cast<int>(card.getRank());
  return aceLow && card.getRank() == Rank::Ace ? -1 : rank;
}

// Every meld the hand could make, as masks over its cards: each set and
// each three-card part of a four-card set, and every stretch of three or
// more in a suit, the ace low or high.
std::vector<Mask> candidateMelds(const std::vector<Card>& hand) {
  const std::size_t n = hand.size();
  std::vector<Mask> melds;
  for (int rank = 0; rank <= static_cast<int>(Rank::Ace); rank++) {
    std::vector<std::size_t> same;
    for (std::size_t i = 0; i < n; i++) {
      if (static_cast<int>(hand[i].getRank()) == rank) same.push_back(i);
    }
    if (same.size() < 3) continue;
    Mask all = 0;
    for (std::size_t i : same) all |= Mask{1} << i;
    melds.push_back(all);
    if (same.size() == 4) {
      for (std::size_t skip : same) melds.push_back(all & ~(Mask{1} << skip));
    }
  }
  for (int suit = 0; suit <= static_cast<int>(Suit::Spades); suit++) {
    for (const bool aceLow : {true, false}) {
      std::vector<std::size_t> cards;
      for (std::size_t i = 0; i < n; i++) {
        if (static_cast<int>(hand[i].getSuit()) == suit) cards.push_back(i);
      }
      std::sort(cards.begin(), cards.end(), [&](std::size_t a, std::size_t b) {
        return rankValue(hand[a], aceLow) < rankValue(hand[b], aceLow);
      });
      for (std::size_t from = 0; from < cards.size(); from++) {
        Mask run = Mask{1} << cards[from];
        for (std::size_t to = from + 1; to < cards.size(); to++) {
          if (rankValue(hand[cards[to]], aceLow) != rankValue(hand[cards[to - 1]], aceLow) + 1) {
            break;
          }
          run |= Mask{1} << cards[to];
          if (to - from >= 2) melds.push_back(run);
        }
      }
    }
  }
  std::sort(melds.begin(), melds.end());
  melds.erase(std::unique(melds.begin(), melds.end()), melds.end());
  return melds;
}

struct Search {
  const std::vector<Card>& hand;
  // Candidates by the lowest card in them: the search decides cards in
  // order, so a meld is taken at its first card or not at all.
  std::vector<std::vector<Mask>> byFirst;
  std::vector<Mask> chosen;
  std::vector<Mask> best;
  int bestPoints;

  int pointsOf(Mask mask) const {
    int sum = 0;
    for (std::size_t i = 0; i < hand.size(); i++) {
      if (mask & (Mask{1} << i)) sum += cardPoints(hand[i]);
    }
    return sum;
  }

  void from(std::size_t i, Mask used, int dead) {
    if (dead >= bestPoints) return;
    while (i < hand.size() && (used & (Mask{1} << i))) i++;
    if (i == hand.size()) {
      bestPoints = dead;
      best = chosen;
      return;
    }
    for (const Mask meld : byFirst[i]) {
      if (meld & used) continue;
      chosen.push_back(meld);
      from(i + 1, used | meld, dead);
      chosen.pop_back();
    }
    from(i + 1, used | (Mask{1} << i), dead + cardPoints(hand[i]));
  }
};

std::vector<Card> cardsOf(const std::vector<Card>& hand, Mask mask) {
  std::vector<Card> cards;
  for (std::size_t i = 0; i < hand.size(); i++) {
    if (mask & (Mask{1} << i)) cards.push_back(hand[i]);
  }
  return cards;
}

bool isSet(const std::vector<Card>& meld) {
  return std::all_of(meld.begin(), meld.end(),
                     [&](const Card& card) { return card.getRank() == meld.front().getRank(); });
}

// Whether `cards` can all go onto `onto`, each onto one meld, leaving
// every meld a meld. A card is only ever tried where it could belong: a
// set of its rank or a run of its suit.
bool laysOff(const std::vector<Card>& cards, const std::vector<std::vector<Card>>& onto) {
  std::vector<std::vector<std::size_t>> fits(cards.size());
  for (std::size_t c = 0; c < cards.size(); c++) {
    for (std::size_t m = 0; m < onto.size(); m++) {
      const bool set = isSet(onto[m]);
      if (set ? cards[c].getRank() == onto[m].front().getRank()
              : cards[c].getSuit() == onto[m].front().getSuit()) {
        fits[c].push_back(m);
      }
    }
    if (fits[c].empty()) return false;
  }
  std::vector<std::size_t> pick(cards.size(), 0);
  for (;;) {
    std::vector<std::vector<Card>> grown = onto;
    for (std::size_t c = 0; c < cards.size(); c++) grown[fits[c][pick[c]]].push_back(cards[c]);
    if (std::all_of(grown.begin(), grown.end(),
                    [](const auto& meld) { return arrangedMeld(meld).has_value(); })) {
      return true;
    }
    std::size_t c = 0;
    while (c < pick.size() && ++pick[c] == fits[c].size()) pick[c++] = 0;
    if (c == pick.size()) return false;
  }
}

}  // namespace

Arrangement bestArrangement(const std::vector<Card>& hand) {
  Search search{hand, std::vector<std::vector<Mask>>(hand.size()), {}, {}, 0};
  for (const Mask meld : candidateMelds(hand)) {
    std::size_t first = 0;
    while (!(meld & (Mask{1} << first))) first++;
    search.byFirst[first].push_back(meld);
  }
  search.bestPoints = search.pointsOf(hand.empty() ? 0 : (Mask{1} << hand.size()) - 1) + 1;
  search.from(0, 0, 0);

  Arrangement out;
  Mask melded = 0;
  for (const Mask meld : search.best) {
    out.melds.push_back(*arrangedMeld(cardsOf(hand, meld)));
    melded |= meld;
  }
  for (std::size_t i = 0; i < hand.size(); i++) {
    if (!(melded & (Mask{1} << i))) out.deadwood.push_back(hand[i]);
  }
  out.deadwoodPoints = search.bestPoints;
  return out;
}

LaidOffArrangement bestWithLayOffs(const std::vector<Card>& hand,
                                   const std::vector<std::vector<Card>>& onto) {
  // Only a card of a set's rank or a run's suit could go on.
  std::vector<std::size_t> candidates;
  for (std::size_t i = 0; i < hand.size(); i++) {
    for (const auto& meld : onto) {
      if (isSet(meld) ? hand[i].getRank() == meld.front().getRank()
                      : hand[i].getSuit() == meld.front().getSuit()) {
        candidates.push_back(i);
        break;
      }
    }
  }
  LaidOffArrangement best{bestArrangement(hand), {}};
  for (Mask pick = 1; pick < (Mask{1} << candidates.size()); pick++) {
    std::vector<Card> laid;
    std::vector<Card> kept;
    Mask off = 0;
    for (std::size_t k = 0; k < candidates.size(); k++) {
      if (pick & (Mask{1} << k)) off |= Mask{1} << candidates[k];
    }
    for (std::size_t i = 0; i < hand.size(); i++) {
      (off & (Mask{1} << i) ? laid : kept).push_back(hand[i]);
    }
    if (!laysOff(laid, onto)) continue;
    Arrangement own = bestArrangement(kept);
    if (own.deadwoodPoints < best.own.deadwoodPoints) best = {std::move(own), std::move(laid)};
  }
  return best;
}

}  // namespace rummy
