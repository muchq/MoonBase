#include "dealer.h"

#include <deque>
#include <random>
#include <vector>

using std::deque;
using std::vector;

namespace cards {
deque<Card> Dealer::DealNewUnshuffledDeck() {
  deque<Card> deck{};
  for (int i = 0; i < 52; i++) {
    deck.emplace_back(i);
  }
  return deck;
}

void Dealer::ShuffleDeck(deque<Card>& deck) {
  vector<int> cards{};
  while (!deck.empty()) {
    cards.emplace_back(deck.front().intValue());
    deck.pop_front();
  }
  std::ranges::shuffle(cards, generator_);

  for (auto& card : cards) {
    deck.emplace_back(card);
  }
}

void NoShuffleDealer::ShuffleDeck(deque<Card>& deck) {}

}  // namespace cards