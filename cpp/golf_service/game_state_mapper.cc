#include "cpp/golf_service/game_state_mapper.h"

#include <string>
#include <unordered_map>

#include "cpp/cards/card.h"

namespace golf {
using namespace cards;

static const std::unordered_map<Rank, std::string> RANK_TO_STRING{
    {Rank::Two, "2"},   {Rank::Three, "3"}, {Rank::Four, "4"}, {Rank::Five, "5"}, {Rank::Six, "6"},
    {Rank::Seven, "7"}, {Rank::Eight, "8"}, {Rank::Nine, "9"}, {Rank::Ten, "10"}, {Rank::Jack, "J"},
    {Rank::Queen, "Q"}, {Rank::King, "K"},  {Rank::Ace, "A"},
};

static const std::unordered_map<Suit, std::string> SUIT_TO_STRING{
    {Suit::Clubs, "C"},
    {Suit::Diamonds, "D"},
    {Suit::Hearts, "H"},
    {Suit::Spades, "S"},
};

class GameStateMapper {
 public:
  static std::string game_state_to_string(const GameState& state);

 private:
  static std::string card_to_string(const Card& cards);
  static std::string cards_to_string(const std::vector<Card>& cards);
  static std::string cards_to_string(const std::deque<Card>& cards);
}

}  // namespace golf