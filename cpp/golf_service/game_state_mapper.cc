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

std::string GameStateMapper::gameStateJson(GameStatePtr state, const std::string& username) {
  std::string output = "{";
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
  const Player& p = state->getPlayer(index);
  output.append(writeString("hand", cm.cardsToString(p.allCards())));
  output.append(",");
  output.append(writeInt("numberOfPlayers", state->getPlayers().size()));
  output.append(",");

  output.append("\"scores\":");
  if (state->isOver()) {
    output.append("[");
    for (size_t i = 0; i < state->getPlayers().size(); i++) {
      auto& p = state->getPlayers().at(i);
      output.append(std::to_string(p.score()));
      if (i < state->getPlayers().size() - 1) {
        output.append(",");
      }
    }
    output.append("],");
  } else {
    output.append("null,");
  }

  output.append(writeString("topDiscard", cm.cardToString(state->getDiscardPile().back())));

  if (state->getPeekedAtDrawPile()) {
    output.append(",");
    output.append(writeString("topDraw", cm.cardToString(state->getDrawPile().back())));
  }

  output.append("}");

  return output;
}

std::string GameStateMapper::writeString(const std::string& name, const std::string& value) {
  return "\"" + name + "\":\"" + value + "\"";
}
std::string GameStateMapper::writeInt(const std::string& name, const int value) {
  return "\"" + name + "\":\"" + std::to_string(value) + "\"";
}
std::string GameStateMapper::writeBool(const std::string& name, const bool value) {
  return "\"" + name + "\":\"" + std::to_string(value) + "\"";
}

}  // namespace golf