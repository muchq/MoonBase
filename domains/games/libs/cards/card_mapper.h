#ifndef CPP_CARDS_CARD_MAPPER_H
#define CPP_CARDS_CARD_MAPPER_H

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace cards {

class CardMapper {
 public:
  std::string cardToString(const Card& c) const;
  std::string cardsToString(const std::vector<Card>& cards) const;

  // TODO: not needed for golf
  absl::StatusOr<Card> cardFromString(const std::string& str) const;
  absl::StatusOr<std::vector<Card>> cardsFromString(const std::string& str) const;
};
}  // namespace cards

#endif
