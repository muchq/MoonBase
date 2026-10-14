#include "domains/games/apis/games_hub/golf_adapter.h"

#include <algorithm>
#include <limits>

#include "absl/strings/str_join.h"
#include "domains/games/apis/games_hub/wire_cards.h"
#include "domains/games/libs/cards/golf/player.h"

namespace games_hub {

std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                     const std::vector<std::string>& roster) {
  std::unordered_set<int> winning;
  int min_score = std::numeric_limits<int>::max();
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    const int score = seat.score();
    if (score < min_score) {
      min_score = score;
      winning.clear();
    }
    if (score == min_score) winning.insert(static_cast<int>(i));
  }
  // The knocker takes ties alone — when still in contention. An
  // abandonment end carries no knocker (kAbandoned is never a seat).
  if (winning.contains(state.getWhoKnocked())) {
    winning.clear();
    winning.insert(state.getWhoKnocked());
  }
  return winning;
}

std::string Adapter<golf::GameState>::Phase(const golf::GameState& state) {
  if (state.isOver()) return "ended";
  if (state.revealCountdownActive()) return "peeking";
  if (state.getWhoKnocked() >= 0) return "knocked";
  return "playing";
}

std::string PlayerIdAt(const golf::GameState& state, int seat) {
  return state.getPlayer(seat).getName().value_or("");
}

std::string Adapter<golf::GameState>::Turn(const golf::GameState& state) {
  return state.isOver() ? "" : PlayerIdAt(state, state.getWhoseTurn());
}

// Roster seats only: an abandoned seat shows on the scorecard but tallies
// nothing.
StatsDeltas Adapter<golf::GameState>::ClosedDeltas(const golf::GameState& state,
                                                   const std::vector<std::string>& roster) {
  const auto winner_indexes = WinnersAmong(state, roster);
  StatsDeltas deltas;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    HubStore::StatsDelta delta;
    delta.player_id = occupant;
    delta.played = 1;
    delta.won = winner_indexes.contains(static_cast<int>(i)) ? 1 : 0;
    delta.score = seat.score();
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

moonbase::games::GameEvents Adapter<golf::GameState>::Wrap(Update update) {
  moonbase::games::GolfEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromGolf(std::move(event));
}

moonbase::games::GameView Adapter<golf::GameState>::ViewOf(const TableContext& context,
                                                           const golf::GameState* engine,
                                                           const std::string& viewer_id) {
  moonbase::games::GameView view;
  view.gameId = context.game_id;

  if (engine == nullptr) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.discardCount = 0;
    view.allPlayersPeeked = false;
    for (const std::string& roster_id : context.roster) {
      moonbase::games::GamePlayer player;
      player.playerId = roster_id;
      player.cards.resize(4);
      player.hasPeeked = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const golf::GameState& state = *engine;
  const bool ended = state.isOver();
  view.phase = Phase(state);
  if (!ended) view.currentPlayerId = PlayerIdAt(state, state.getWhoseTurn());
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.discardCount = static_cast<int>(state.getDiscardPile().size());
  if (!state.getDiscardPile().empty()) view.discardTop = WireCard(state.getDiscardPile().back());
  // Only a real seat's knock is advertised: an abandonment end carries
  // the kAbandoned sentinel, not a knock somebody would be blamed for.
  if (state.getWhoKnocked() >= 0) view.knockedPlayerId = PlayerIdAt(state, state.getWhoKnocked());
  view.allPlayersPeeked = state.allPlayersPeeked();
  // The drawn card rides only to the player who is looking at it.
  if (state.getPeekedAtDrawPile() && !ended &&
      PlayerIdAt(state, state.getWhoseTurn()) == viewer_id && !state.getDrawPile().empty()) {
    view.drawnCard = WireCard(state.getDrawPile().back());
  }

  for (const golf::Player& seat : state.getPlayers()) {
    const std::string occupant = seat.getName().value_or("");
    moonbase::games::GamePlayer player;
    player.playerId = occupant;
    player.cards.resize(4);
    player.hasPeeked = seat.hasCompletedPeeks();
    if (ended) {
      const std::vector<cards::Card> hand = seat.allCards();
      for (int i = 0; i < 4; ++i) {
        player.cards[i].card = WireCard(hand[static_cast<std::size_t>(i)]);
        player.revealedIndexes.push_back(i);
      }
      player.score = seat.score();
    } else if (occupant == viewer_id) {
      for (const golf::Position position : seat.getPeeked()) {
        const int index = golf::indexOfPosition(position);
        player.cards[static_cast<std::size_t>(index)].card = WireCard(seat.cardAt(position));
        player.revealedIndexes.push_back(index);
      }
    }
    view.players.push_back(std::move(player));
  }
  return view;
}

moonbase::games::GameEvents Adapter<golf::GameState>::StateEvent(View view) {
  moonbase::games::GameStateUpdate update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<golf::GameState>::JoinedEvent(View view) {
  moonbase::games::GameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Seat order, so the display string is stable. Winners come from the
// roster's seats; the scores keep every seat, abandoned or not.
std::optional<moonbase::games::GameEvents> Adapter<golf::GameState>::ClosedEvent(
    const golf::GameState& state, const std::vector<std::string>& roster) {
  const auto winner_indexes = WinnersAmong(state, roster);
  std::vector<std::string> winner_ids;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    if (winner_indexes.contains(static_cast<int>(i))) {
      winner_ids.push_back(state.getPlayer(static_cast<int>(i)).getName().value_or(""));
    }
  }

  moonbase::games::GameEnded ended;
  ended.winner = absl::StrJoin(winner_ids, " & ");
  ended.winners = winner_ids;
  for (const golf::Player& seat : state.getPlayers()) {
    moonbase::games::FinalScore score;
    score.playerId = seat.getName().value_or("");
    score.score = seat.score();
    ended.finalScores.push_back(std::move(score));
  }
  return Wrap(Update::FromGameended(std::move(ended)));
}

}  // namespace games_hub
