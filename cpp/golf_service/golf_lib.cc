#include "golf_lib.h"

#include <unordered_set>
#include <vector>

#include "cpp/cards/card.h"

using namespace cards;

const int golf::Player::score() {
  std::unordered_set<Rank> hand;
  int score = 0;
  for (auto c : allCards()) {
    if (hand.find(c.getRank()) != hand.end()) {  // pairs cancel each other
      score -= cardValue(c);
      hand.erase(c.getRank());
    } else {
      score += cardValue(c);
      hand.insert(c.getRank());
    }
  }
  return score;
}

const std::vector<Card> golf::Player::allCards() {
  std::vector<Card> all;
  all.push_back(topLeft);
  all.push_back(topRight);
  all.push_back(bottomLeft);
  all.push_back(bottomRight);
  return all;
}

const int golf::Player::cardValue(Card c) {
  switch (c.getRank()) {
    case cards::Rank::Ace:
      return 1;
    case cards::Rank::Two:
      return 2;
    case cards::Rank::Three:
      return 3;
    case cards::Rank::Four:
      return 4;
    case cards::Rank::Five:
      return 5;
    case cards::Rank::Six:
      return 6;
    case cards::Rank::Seven:
      return 7;
    case cards::Rank::Eight:
      return 8;
    case cards::Rank::Nine:
      return 9;
    case cards::Rank::Ten:
      return 10;
    case cards::Rank::Jack:
      return 0;
    case cards::Rank::Queen:
      return 10;
    case cards::Rank::King:
      return 10;
    default:
      return -1;  // error
  }
}