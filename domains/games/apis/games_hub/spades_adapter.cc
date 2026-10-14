#include "domains/games/apis/games_hub/spades_adapter.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/wire_cards.h"

namespace games_hub {
namespace {

// Every seat played the game, and the winning team's seats won it. A game
// a leave abandoned was played, and won by nobody.
StatsDeltas GameDeltas(const spades::Table& table, std::optional<int> winning_team) {
  StatsDeltas deltas;
  for (int seat = 0; seat < table.hand().seats(); ++seat) {
    HubStore::StatsDelta delta;
    delta.player_id = table.seats()[seat];
    delta.played = 1;
    delta.won = winning_team == table.teamOf(seat) ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

// A spades game's end as the wire spells it: the winning team's seats.
moonbase::games::SpadesResult SpadesResultOf(const spades::Table& table,
                                             const spades::GameScore& game) {
  moonbase::games::SpadesResult result;
  result.ending = game.winner.has_value() ? "points" : "abandoned";
  for (int seat = 0; seat < table.hand().seats(); ++seat) {
    if (game.winner == table.teamOf(seat)) result.winners.push_back(table.seats()[seat]);
  }
  result.scores = game.scores;
  return result;
}

}  // namespace

std::string Adapter<spades::Table>::Turn(const spades::Table& table) {
  // A closed table has no turn; between hands the hand names none.
  const int seat = table.isOver() ? spades::kNoTurn : table.hand().turn();
  return seat < 0 ? "" : table.seats().at(seat);
}

// A table closes by a leave: a game it abandoned was played; one already
// won was credited as it was won.
StatsDeltas Adapter<spades::Table>::ClosedDeltas(const spades::Table& table,
                                                 const std::vector<std::string>&) {
  return table.abandonedGame() ? GameDeltas(table, std::nullopt) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<spades::Table>::DealEndDeltas(const spades::Table& before,
                                                                 const spades::Table& after) {
  if (before.phase() == spades::Phase::kWon || after.phase() != spades::Phase::kWon) {
    return std::nullopt;
  }
  return GameDeltas(after, after.winner());
}

moonbase::games::GameEvents Adapter<spades::Table>::Wrap(Update update) {
  moonbase::games::SpadesEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromSpades(std::move(event));
}

// Its own hand by suit (♠ ♥ ♣ ♦, colours alternating) and rank.
moonbase::games::SpadesView Adapter<spades::Table>::ViewOf(const TableContext& context,
                                                           const spades::Table* engine,
                                                           const std::string& viewer_id) {
  moonbase::games::SpadesView view;
  view.gameId = context.game_id;
  view.handNumber = 0;
  view.spadesBroken = false;
  if (engine == nullptr) {
    view.phase = "waiting";
    for (std::size_t i = 0; i < context.roster.size(); ++i) {
      moonbase::games::SpadesPlayer player;
      player.playerId = context.roster[i];
      player.team = 0;
      player.handCount = 0;
      player.tricks = 0;
      view.players.push_back(std::move(player));
    }
    return view;
  }
  const spades::Table& table = *engine;
  const spades::Hand& hand = table.hand();
  const auto id_of = [&table](int seat) { return table.seats().at(seat); };
  view.phase = std::string(spades::phaseName(table.phase()));
  view.mode = std::string(spades::modeName(table.mode()));
  view.handSize = spades::handSize(hand.seats());
  view.dealer = id_of(hand.dealer());
  const std::string turn = Turn(table);
  if (!turn.empty()) view.currentPlayerId = turn;
  view.handNumber = table.handNumber();
  view.spadesBroken = hand.spadesBroken();
  static constexpr std::array<int, 4> kSuitOrder = {2, 3, 1, 0};  // ♣ ♦ ♥ ♠ → ♠ ♥ ♣ ♦
  const auto sorted = [](std::vector<cards::Card> cards) {
    std::sort(cards.begin(), cards.end(), [](const cards::Card& a, const cards::Card& b) {
      const int suit_a = kSuitOrder[static_cast<int>(a.getSuit())];
      const int suit_b = kSuitOrder[static_cast<int>(b.getSuit())];
      if (suit_a != suit_b) return suit_a < suit_b;
      return a.getRank() < b.getRank();
    });
    std::vector<moonbase::games::Card> wire;
    for (const cards::Card& card : cards) wire.push_back(WireCard(card));
    return wire;
  };
  const int viewer = table.playerIndex(viewer_id);
  for (int seat = 0; seat < hand.seats(); ++seat) {
    moonbase::games::SpadesPlayer player;
    player.playerId = id_of(seat);
    player.team = table.teamOf(seat);
    player.handCount = static_cast<int>(hand.hands()[seat].size());
    if (seat == viewer) player.hand = sorted(hand.hands()[seat]);
    player.bid = hand.bids()[seat];
    player.tricks = hand.tricks()[seat];
    view.players.push_back(std::move(player));
  }
  const auto plays_of = [&](const std::vector<spades::TrickPlay>& plays) {
    std::vector<moonbase::games::SpadesTrickPlay> wire;
    for (const spades::TrickPlay& play : plays) {
      moonbase::games::SpadesTrickPlay played;
      played.playerId = id_of(play.seat);
      played.card = WireCard(play.card);
      wire.push_back(std::move(played));
    }
    return wire;
  };
  view.trick = plays_of(hand.current());
  if (hand.last().has_value()) {
    moonbase::games::SpadesTrick last;
    last.plays = plays_of(hand.last()->plays);
    last.winner = id_of(hand.last()->winner);
    view.lastTrick = std::move(last);
  }
  if (viewer >= 0 && !table.isOver()) view.legal = sorted(hand.legalPlays(viewer));
  for (int team = 0; team < table.teams(); ++team) {
    moonbase::games::SpadesTeam side;
    for (int seat = team; seat < hand.seats(); seat += table.teams()) {
      side.players.push_back(id_of(seat));
    }
    side.score = table.scores()[team];
    side.bags = table.bags()[team];
    side.gamesWon = table.gamesWon()[team];
    view.teams.push_back(std::move(side));
  }
  for (const spades::HandLine& line : table.hands()) {
    moonbase::games::SpadesHandLine wire;
    wire.bids.assign(line.bids.begin(), line.bids.end());
    wire.tricks.assign(line.tricks.begin(), line.tricks.end());
    wire.points.assign(line.points.begin(), line.points.end());
    wire.scores.assign(line.scores.begin(), line.scores.end());
    view.hands.push_back(std::move(wire));
  }
  if (const std::optional<spades::GameScore> result = table.result(); result.has_value()) {
    view.result = SpadesResultOf(table, *result);
  }
  if (table.left().has_value()) view.left = id_of(*table.left());
  return view;
}

moonbase::games::GameEvents Adapter<spades::Table>::StateEvent(View view) {
  moonbase::games::SpadesGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<spades::Table>::JoinedEvent(View view) {
  moonbase::games::SpadesGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Read from the table's games, so a game that ended before the next began
// is owed all the same.
std::vector<moonbase::games::GameEvents> Adapter<spades::Table>::GamesEnded(
    const spades::Table& table, std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  for (std::size_t i = told; i < table.games().size(); ++i) {
    moonbase::games::SpadesGameEnded ended;
    ended.result = SpadesResultOf(table, table.games()[i]);
    events.push_back(Wrap(Update::FromGameended(std::move(ended))));
  }
  return events;
}

// A play names its card: a card the hand does not hold is the stale-view
// refusal, named in the wire's spelling.
Action<spades::Table, Adapter<spades::Table>::Start> Adapter<spades::Table>::Decode(
    const Move& move, const std::string&) {
  using SpadesTransition = Transition<spades::Table>;
  if (const auto* start = move.as_startGame_or_null()) {
    Start named;
    if (start->mode.has_value()) {
      named.mode = spades::parseMode(*start->mode);
      if (!named.mode.has_value()) {
        return Refusal{RejectKind::kRules, absl::StrCat("unknown spades mode: ", *start->mode)};
      }
    }
    return StartGame<Start>{named};
  }
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  if (const auto* bid = move.as_bid_or_null()) {
    return SpadesTransition{
        [tricks = bid->tricks](const spades::Table& table, int seat, const MoveContext&) {
          return table.bid(seat, tricks);
        }};
  }
  if (const auto* play = move.as_play_or_null()) {
    const std::optional<cards::Card> card = CardFromWire(play->card);
    if (!card.has_value()) {
      return Refusal{RejectKind::kInvalid,
                     absl::StrCat("no such card: ", play->card.rank, play->card.suit)};
    }
    return SpadesTransition{[card = *card](const spades::Table& table, int seat,
                                           const MoveContext&) -> absl::StatusOr<spades::Table> {
      absl::StatusOr<spades::Table> next = table.play(seat, card);
      if (next.status().code() == absl::StatusCode::kNotFound) {
        return HeldInHand(table.hand().hands()[seat], {card});
      }
      return next;
    }};
  }
  if (move.as_deal_or_null() != nullptr) {
    return SpadesTransition{[](const spades::Table& table, int, const MoveContext& context) {
      return table.deal(context.deck());
    }};
  }
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

}  // namespace games_hub
