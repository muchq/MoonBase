#include "domains/games/apis/games_hub/wire_cards.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/card_mapper.h"

namespace games_hub {
namespace {

using cards::Card;
using cards::Suit;

constexpr int kDeckSize = 52;

constexpr std::array<std::pair<Suit, const char*>, 4> kSuitGlyphs{{
    {Suit::Clubs, "♣"},
    {Suit::Diamonds, "♦"},
    {Suit::Hearts, "♥"},
    {Suit::Spades, "♠"},
}};

std::string SuitGlyph(Suit suit) {
  for (const auto& [s, glyph] : kSuitGlyphs) {
    if (s == suit) return glyph;
  }
  return "♠";
}

std::string Spell(const Card& card) {
  const moonbase::games::Card wire = WireCard(card);
  return absl::StrCat(wire.rank, wire.suit);
}

std::string Spell(const moonbase::games::Card& wire) { return absl::StrCat(wire.rank, wire.suit); }

}  // namespace

moonbase::games::Card WireCard(const Card& card) {
  moonbase::games::Card wire;
  wire.rank = cards::CardMapper::rankToString(card.getRank());
  wire.suit = SuitGlyph(card.getSuit());
  return wire;
}

std::optional<Card> CardFromWire(const moonbase::games::Card& wire) {
  for (int i = 0; i < kDeckSize; ++i) {
    const Card card{i};
    const moonbase::games::Card spelled = WireCard(card);
    if (spelled.rank == wire.rank && spelled.suit == wire.suit) return card;
  }
  return std::nullopt;
}

absl::StatusOr<std::vector<Card>> CardsFromWire(const std::vector<moonbase::games::Card>& named) {
  std::vector<Card> cards;
  cards.reserve(named.size());
  for (const moonbase::games::Card& wire : named) {
    const std::optional<Card> card = CardFromWire(wire);
    if (!card.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("no such card: ", Spell(wire)));
    }
    if (std::find(cards.begin(), cards.end(), *card) != cards.end()) {
      return absl::InvalidArgumentError(absl::StrCat("named twice: ", Spell(wire)));
    }
    cards.push_back(*card);
  }
  return cards;
}

absl::StatusOr<std::vector<int>> RowIndexesOf(const std::vector<Card>& row,
                                              const std::vector<Card>& named) {
  std::vector<int> indexes;
  indexes.reserve(named.size());
  for (const Card& card : named) {
    int found = -1;
    for (int i = 0; i < static_cast<int>(row.size()); ++i) {
      if (!(row[i] == card)) continue;
      if (found >= 0) {
        return absl::NotFoundError(absl::StrCat("two of ", Spell(card), " in that row"));
      }
      found = i;
    }
    if (found < 0) {
      return absl::NotFoundError(absl::StrCat("not in that row: ", Spell(card)));
    }
    indexes.push_back(found);
  }
  std::sort(indexes.begin(), indexes.end());
  return indexes;
}

}  // namespace games_hub
