#ifndef CPP_CARDS_CARD_MAPPER_H
#define CPP_CARDS_CARD_MAPPER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"

namespace cards {

class CardMapper {
 public:
  static std::string card_to_string(const Card& c);
  static std::string cards_to_string(const std::vector<Card>& cards);

  // TODO: not needed for golf
  static absl::StatusOr<Card> card_from_string(const std::string& str);
  static absl::StatusOr<std::vector<Card>> cards_from_string(const std::string& str);
};
}  // namespace cards

#endif