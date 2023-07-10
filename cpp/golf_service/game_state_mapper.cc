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

std::string GameStateMapper::gameStateToString(GameStatePtr state) { return ""; }

}  // namespace golf