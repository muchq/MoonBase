#include "cpp/golf_service/game_state_mapper.h"

#include <memory>
#include <string>
#include <unordered_map>

#include "cpp/cards/card.h"

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
                                                             const string& username) {
  golf_ws::GameStateResponse proto;
  proto.set_all_here(state->allPlayersPresent());
  proto.set_discard_size(state->getDiscardPile().size());
  proto.set_draw_size(state->getDrawPile().size());
  proto.set_game_id(state->getGameId());
  proto.set_game_over(state->isOver());
  const Player& knocker = state->getPlayer(state->getWhoKnocked());
  if (knocker.getName().has_value()) {
    proto.set_knocker(knocker.getName().value());
  }

  const int index = state->playerIndex(username);
  const Player& player = state->getPlayer(index);
  const auto& cards = player.allCards();

  // parent proto will take ownership and free this appropriately
  golf_ws::VisibleHand* hand = new golf_ws::VisibleHand;
  hand->set_bottom_left(CardMapper::cardToString(cards.at(3)));
  hand->set_bottom_right(CardMapper::cardToString(cards.at(4)));
  proto.set_allocated_hand(hand);
  proto.set_number_of_players(state->getPlayers().size());

  if (state->isOver()) {
    for (auto& p : state->getPlayers()) {
      proto.add_scores(p.score());
    }
  }

  proto.set_top_discard(CardMapper::cardToString(state->getDiscardPile().back()));

  if (state->getPeekedAtDrawPile() && state->getWhoseTurn() == index) {
    proto.set_top_draw(CardMapper::cardToString(state->getDrawPile().back()));
  }

  proto.set_your_turn(state->getWhoseTurn() == index);

  return proto;
}

string GameStateMapper::gameStateJson(const GameStatePtr& state, const string& username) {
  string output("{");
  output.append(writeBool("allHere", state->allPlayersPresent()));
  output.append(",");
  output.append(writeInt("discardSize", state->getDiscardPile().size()));
  output.append(",");
  output.append(writeInt("drawSize", state->getDrawPile().size()));
  output.append(",");
  output.append(writeString("gameId", state->getGameId()));
  output.append(",");
  output.append(writeBool("gameOver", state->isOver()));
  output.append(",");
  if (state->getWhoKnocked() != -1) {
    const Player& knocker = state->getPlayer(state->getWhoKnocked());
    if (knocker.getName().has_value()) {
      output.append(writeString("knocker", knocker.getName().value()));
    } else {
      output.append(writeString("knocker", "_"));
    }
    output.append(",");
  }

  const int index = state->playerIndex(username);
  const Player& player = state->getPlayer(index);

  output.append("\"hand\":[");
  size_t cardIndex = 0;
  while (cardIndex < 4) {
    output.append("\"");
    output.append(CardMapper::cardToString(player.allCards().at(cardIndex)));
    output.append("\"");
    if (cardIndex < 3) {
      output.append(",");
    }
    cardIndex++;
  }
  output.append("],");
  output.append(writeInt("numberOfPlayers", state->getPlayers().size()));
  output.append(",");

  if (state->isOver()) {
    output.append("\"scores\":");
    output.append("[");
    for (size_t i = 0; i < state->getPlayers().size(); i++) {
      auto& p = state->getPlayers().at(i);
      output.append(std::to_string(p.score()));
      if (i < state->getPlayers().size() - 1) {
        output.append(",");
      }
    }
    output.append("],");
  }
  output.append(
      writeString("topDiscard", CardMapper::cardToString(state->getDiscardPile().back())));
  output.append(",");

  if (state->getPeekedAtDrawPile() && state->getWhoseTurn() == index) {
    output.append(writeString("topDraw", CardMapper::cardToString(state->getDrawPile().back())));
    output.append(",");
  }
  output.append(writeBool("yourTurn", state->getWhoseTurn() == index));
  output.append("}");
  return output;
}

string GameStateMapper::writeString(const string& name, const string& value) {
  return "\"" + name + "\":\"" + value + "\"";
}

string GameStateMapper::writeInt(const string& name, const int value) {
  return "\"" + name + "\":" + std::to_string(value);
}

string GameStateMapper::writeBool(const string& name, const bool value) {
  if (value) {
    return "\"" + name + "\":true";
  } else {
    return "\"" + name + "\":false";
  }
}

}  // namespace golf