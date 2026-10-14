#include "domains/games/apis/games_hub/castle_adapter.h"

#include "domains/games/apis/games_hub/wire_cards.h"

namespace games_hub {

std::string Adapter<castle::GameState>::Phase(const castle::GameState& state) {
  switch (state.getPhase()) {
    case castle::Phase::Setup:
      return "setup";
    case castle::Phase::Playing:
      return "playing";
    case castle::Phase::Over:
    case castle::Phase::Abandoned:
      return "ended";
  }
  return "ended";
}

std::string Adapter<castle::GameState>::Turn(const castle::GameState& state) {
  const int seat = state.getWhoseTurn();
  if (seat < 0 || seat >= static_cast<int>(state.getPlayers().size())) return "";
  return state.getPlayer(seat).getId();
}

// The first out wins and everyone seated played. The engine compacts a
// leaver out of its seats, so every seat is on the roster; and a finish
// ends the game, so the winner never left. This leans on an over table
// never being left: StageGameOverLocked erases the entry before anyone
// could. No score: the game has none, so the room's running total stays
// put.
StatsDeltas Adapter<castle::GameState>::ClosedDeltas(const castle::GameState& state,
                                                     const std::vector<std::string>&) {
  std::vector<std::string> seated;
  for (const castle::Player& seat : state.getPlayers()) seated.push_back(seat.getId());
  return PlayedWon(seated, state.getFinished().empty() ? "" : state.getFinished().front());
}

moonbase::games::GameEvents Adapter<castle::GameState>::Wrap(Update update) {
  moonbase::games::CastleEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromCastle(std::move(event));
}

moonbase::games::CastleView Adapter<castle::GameState>::ViewOf(const TableContext& context,
                                                               const castle::GameState* engine,
                                                               const std::string& viewer_id) {
  moonbase::games::CastleView view;
  view.gameId = context.game_id;
  if (engine == nullptr) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.pileCount = 0;
    for (const std::string& roster_id : context.roster) {
      moonbase::games::CastlePlayer player;
      player.playerId = roster_id;
      player.ready = false;
      player.handCount = 0;
      player.faceDownCount = 0;
      player.out = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const castle::GameState& state = *engine;
  const bool ended = state.isOver();
  view.phase = Phase(state);
  const std::string current = Turn(state);
  if (!ended && !current.empty()) view.currentPlayerId = current;
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.pileCount = static_cast<int>(state.getPile().size());
  // The run on top, top last: the price of the next play, and the cards
  // a table shows.
  const std::vector<cards::Card>& pile = state.getPile();
  for (std::size_t i = pile.size() - state.runOnTop(); i < pile.size(); ++i) {
    view.run.push_back(WireCard(pile[i]));
  }
  view.finished = state.getFinished();
  if (const auto& play = state.getLastPlay(); play.has_value()) {
    moonbase::games::CastleLastPlay last;
    last.playerId = play->playerId;
    for (const cards::Card& card : play->cards) last.cards.push_back(WireCard(card));
    last.burned = play->burned;
    last.pickedUp = play->pickedUp;
    view.lastPlay = std::move(last);
  }
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const castle::Player& seat = state.getPlayer(static_cast<int>(i));
    moonbase::games::CastlePlayer player;
    player.playerId = seat.getId();
    player.ready = seat.isReady();
    player.handCount = static_cast<int>(seat.getHand().size());
    // The viewer's own seat, on turn: whether a play exists, or else
    // the pile is theirs to pick up (a blind row flips instead). Nobody
    // else's hand is the viewer's to read, and an over game seats nobody
    // on turn.
    player.canPlay = seat.getId() == viewer_id && state.getWhoseTurn() == static_cast<int>(i) &&
                     state.hasLegalPlay(static_cast<int>(i));
    // Own hand faces only, everyone's once the game ends; face-down
    // rows are a count for everyone, their holder included.
    if (ended || seat.getId() == viewer_id) {
      for (const cards::Card& card : seat.getHand()) player.hand.push_back(WireCard(card));
    }
    for (const cards::Card& card : seat.getFaceUp()) player.faceUp.push_back(WireCard(card));
    player.faceDownCount = static_cast<int>(seat.getFaceDown().size());
    player.out = seat.isOut();
    view.players.push_back(std::move(player));
  }
  return view;
}

moonbase::games::GameEvents Adapter<castle::GameState>::StateEvent(View view) {
  moonbase::games::CastleGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<castle::GameState>::JoinedEvent(View view) {
  moonbase::games::CastleGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// The finish order; the engine names a loser only for a game over by
// play, and an abandonment has no finish order at all — nobody went out.
std::optional<moonbase::games::GameEvents> Adapter<castle::GameState>::ClosedEvent(
    const castle::GameState& state, const std::vector<std::string>&) {
  moonbase::games::CastleGameEnded ended;
  ended.finished = state.getFinished();
  ended.loser = state.loser();
  return Wrap(Update::FromGameended(std::move(ended)));
}

}  // namespace games_hub
