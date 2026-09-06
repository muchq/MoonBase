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
using cards::Rank;
using cards::Suit;

constexpr std::array<std::pair<Suit, const char*>, 4> kSuitGlyphs{{
    {Suit::Clubs, "♣"},
    {Suit::Diamonds, "♦"},
    {Suit::Hearts, "♥"},
    {Suit::Spades, "♠"},
}};

constexpr std::array<Rank, 13> kRanks{Rank::Two,   Rank::Three, Rank::Four, Rank::Five, Rank::Six,
                                      Rank::Seven, Rank::Eight, Rank::Nine, Rank::Ten,  Rank::Jack,
                                      Rank::Queen, Rank::King,  Rank::Ace};

std::string SuitGlyph(Suit suit) {
  for (const auto& [s, glyph] : kSuitGlyphs) {
    if (s == suit) return glyph;
  }
  return "♠";
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
  for (const auto& [suit, glyph] : kSuitGlyphs) {
    if (wire.suit != glyph) continue;
    for (const Rank rank : kRanks) {
      if (cards::CardMapper::rankToString(rank) == wire.rank) return Card{suit, rank};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

absl::StatusOr<std::vector<int>> RowIndexesOf(const std::vector<Card>& row,
                                              const std::vector<moonbase::games::Card>& named) {
  std::vector<int> indexes;
  indexes.reserve(named.size());
  for (const moonbase::games::Card& wire : named) {
    const std::optional<Card> card = CardFromWire(wire);
    if (!card.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("no such card: ", Spell(wire)));
    }
    int found = -1;
    for (int i = 0; i < static_cast<int>(row.size()); ++i) {
      if (!(row[i] == *card)) continue;
      if (found >= 0) {
        return absl::InvalidArgumentError(absl::StrCat("two of ", Spell(wire), " in that row"));
      }
      found = i;
    }
    if (found < 0) {
      return absl::InvalidArgumentError(absl::StrCat("not in that row: ", Spell(wire)));
    }
    if (std::find(indexes.begin(), indexes.end(), found) != indexes.end()) {
      return absl::InvalidArgumentError(absl::StrCat("named twice: ", Spell(wire)));
    }
    indexes.push_back(found);
  }
  std::sort(indexes.begin(), indexes.end());
  return indexes;
}

}  // namespace games_hub
