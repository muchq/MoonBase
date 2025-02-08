#ifndef CPP_CARDS_DEALER_H
#define CPP_CARDS_DEALER_H

#include <deque>
#include <random>

#include "cpp/cards/card.h"
#include "protos/cards/cards.pb.h"

namespace cards {

class Dealer {
 public:
  virtual ~Dealer() = default;
  std::deque<Card> DealNewUnshuffledDeck();
  virtual void ShuffleDeck(std::deque<Card>& deck);

 private:
  std::random_device rd_;
  std::mt19937 generator_{rd_()};
};

class NoShuffleDealer : public Dealer {
 public:
  void ShuffleDeck(std::deque<Card>& deck) override;
};

}  // namespace cards

#endif
