#include "domains/games/apis/golf_service/game_state_mapper.h"

#include <memory>
#include <string>
#include <unordered_map>

#include "domains/games/libs/cards/card.h"

namespace golf {
using namespace cards;
using std::string;
using std::unordered_map;

static const unordered_map<Rank, string> RANK_TO_STRING{
    {Rank::Two, "2"},   {Rank::Three, "3"}, {Rank::Four, "4"}, {Rank::Five, "5"}, {Rank::Six, "6"},
    {Rank::Seven, "7"}, {Rank::Eight, "8"}, {Rank::Nine, "9"}, {Rank::Ten, "10"}, {Rank::Jack, "J"},
    {Rank::Queen, "Q"}, {Rank::King, "K"},  {Rank::Ace, "A"},
};

static const unordered_map<Suit, string> SUIT_TO_STRING{
    {Suit::Clubs, "C"},
    {Suit::Diamonds, "D"},
    {Suit::Hearts, "H"},
    {Suit::Spades, "S"},
};

golf_ws::GameStateResponse GameStateMapper::gameStateToProto(const GameStatePtr& state,
                                                             const string& username) const {
  golf_ws::GameStateResponse proto;
  proto.set_all_here(state->allPlayersPresent());
  proto.set_discard_size(state->getDiscardPile().size());
  proto.set_draw_size(state->getDrawPile().size());
  proto.set_game_id(state->getGameId());
  proto.set_game_over(state->isOver());

  int knockIndex = state->getWhoKnocked();
  if (knockIndex != -1) {
    const Player& knocker = state->getPlayer(knockIndex);
    if (knocker.getName().has_value()) {
      proto.set_knocker(knocker.getName().value());
    }
  }

  const int index = state->playerIndex(username);
  const Player& player = state->getPlayer(index);
  const auto& cards = player.allCards();

  // parent proto will take ownership and free this appropriately
  auto hand = new golf_ws::VisibleHand;
  hand->set_bottom_left(card_mapper.cardToString(cards.at(2)));
  hand->set_bottom_right(card_mapper.cardToString(cards.at(3)));
  proto.set_allocated_hand(hand);
  proto.set_number_of_players(state->getPlayers().size());

  if (state->isOver()) {
    for (auto& p : state->getPlayers()) {
      proto.add_scores(p.score());
    }
  }

  proto.set_top_discard(card_mapper.cardToString(state->getDiscardPile().back()));

  if (state->getPeekedAtDrawPile() && state->getWhoseTurn() == index) {
    proto.set_top_draw(card_mapper.cardToString(state->getDrawPile().back()));
  }

  proto.set_your_turn(state->getWhoseTurn() == index);

  return proto;
}

}  // namespace golf
