#include "domains/games/apis/games_hub/go_adapter.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/round_robin_wire.h"
#include "domains/games/libs/go/table_serde.h"

namespace games_hub {
namespace {

// Both seats played, the winner won, a draw credits nobody. A leaver
// played and lost.
StatsDeltas GameDeltas(const go::GameState& state) {
  const std::optional<go::Result>& result = state.result();
  std::string winner;
  if (result.has_value() && result->winner.has_value()) {
    winner = state.players().at(state.seatOf(*result->winner));
  }
  return PlayedWon(state.players(), winner);
}

moonbase::games::GoScore WireScore(const go::Score& score);

moonbase::games::GoResult WireResult(const go::GameState& state) {
  const go::Result& result = *state.result();
  moonbase::games::GoResult wire;
  wire.ending = std::string(go::EndingName(result.ending));
  if (result.winner.has_value()) {
    wire.winner = state.players().at(state.seatOf(*result.winner));
    wire.winnerColor = std::string(go::ColorName(*result.winner));
  }
  if (result.ending == go::Ending::kScore) wire.score = WireScore(state.score());
  return wire;
}

moonbase::games::GoScore WireScore(const go::Score& score) {
  moonbase::games::GoScore wire;
  wire.black = score.black;
  wire.white = score.white;
  return wire;
}

// A startGame's or a challenge's fields, absent ones taking the defaults.
GoTerms TermsFrom(const moonbase::games::GoStartGame& fields) {
  GoTerms terms;
  if (fields.size.has_value()) terms.size = *fields.size;
  if (fields.komi.has_value()) terms.komi = *fields.komi;
  if (fields.initialSeconds.has_value()) {
    terms.time_control.initial_ms = int64_t{*fields.initialSeconds} * 1000;
  }
  if (fields.incrementSeconds.has_value()) {
    terms.time_control.increment_ms = int64_t{*fields.incrementSeconds} * 1000;
  }
  return terms;
}

}  // namespace

// A game in play, one being scored, one ended with the table waiting on
// the next, or the table closed by a leave.
std::string Adapter<go::Table>::Phase(const go::Table& table) {
  if (table.isOver()) return "closed";
  if (table.game().scoring()) return "scoring";
  return table.game().isOver() ? "ended" : "playing";
}

std::string Adapter<go::Table>::Turn(const go::Table& table) {
  const int seat = table.game().whoseTurn();
  return seat < 0 ? "" : table.players().at(seat);
}

// A close by a leave that ended a game credits it as any end does; one
// between games, nothing.
StatsDeltas Adapter<go::Table>::ClosedDeltas(const go::Table& table,
                                             const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table.game()) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<go::Table>::DealEndDeltas(const go::Table& before,
                                                             const go::Table& after) {
  if (before.game().isOver() || !after.game().isOver() || after.isOver()) return std::nullopt;
  return GameDeltas(after.game());
}

moonbase::games::GameEvents Adapter<go::Table>::Wrap(Update update) {
  moonbase::games::GoEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromGo(std::move(event));
}

moonbase::games::GoView Adapter<go::Table>::ViewOf(const TableContext& context,
                                                   const go::Table* table, const std::string&) {
  moonbase::games::GoView view;
  view.gameId = context.game_id;
  view.moveCount = 0;
  if (context.event.has_value()) view.roundRobinId = context.event->event_id;
  if (table == nullptr) {
    view.phase = "waiting";
    if (context.terms.has_value()) view.terms = WireGoTerms(std::get<GoTerms>(*context.terms));
    for (const std::string& roster_id : context.roster) {
      moonbase::games::GoPlayer player;
      player.playerId = roster_id;
      view.players.push_back(std::move(player));
    }
    return view;
  }
  const go::GameState& state = table->game();
  view.phase = Phase(*table);
  view.size = state.size();
  view.komi = state.komi();
  for (const go::Color color : {go::Color::kBlack, go::Color::kWhite}) {
    moonbase::games::GoPlayer player;
    player.playerId = state.players().at(state.seatOf(color));
    player.color = std::string(go::ColorName(color));
    view.players.push_back(std::move(player));
  }
  for (int y = 0; y < state.size(); ++y) {
    view.board.push_back(state.board().substr(static_cast<std::size_t>(y) * state.size(),
                                              static_cast<std::size_t>(state.size())));
  }
  if (!state.isOver()) {
    view.sideToMove = std::string(go::ColorName(state.sideToMove()));
    view.currentPlayerId = state.players().at(state.whoseTurn());
  }
  moonbase::games::GoCaptures captures;
  captures.black = state.captures(go::Color::kBlack);
  captures.white = state.captures(go::Color::kWhite);
  view.captures = captures;
  const std::vector<int>& moves = state.moves();
  view.moveCount = static_cast<int>(moves.size());
  if (!moves.empty()) {
    // Black moves first, so the last move's colour is the parity's.
    moonbase::games::GoLastMove last;
    last.color =
        std::string(go::ColorName(moves.size() % 2 == 1 ? go::Color::kBlack : go::Color::kWhite));
    last.pass = moves.back() == go::kPass;
    if (!last.pass) {
      last.x = moves.back() % state.size();
      last.y = moves.back() / state.size();
    }
    view.lastMove = std::move(last);
  }
  moonbase::games::GoClock clock;
  clock.blackMs = state.remainingMs(go::Color::kBlack, context.now_ms);
  clock.whiteMs = state.remainingMs(go::Color::kWhite, context.now_ms);
  clock.initialMs = state.timeControl().initial_ms;
  clock.incrementMs = state.timeControl().increment_ms;
  view.clock = clock;
  if (!state.dead().empty()) {
    std::vector<moonbase::games::GoPoint> dead;
    for (const int point : state.dead()) {
      moonbase::games::GoPoint at;
      at.x = point % state.size();
      at.y = point / state.size();
      dead.push_back(at);
    }
    view.dead = std::move(dead);
  }
  if (state.scoring()) {
    moonbase::games::GoScoring scoring;
    for (int seat = 0; seat < go::GameState::kSeats; ++seat) {
      if (state.accepted()[seat]) scoring.acceptedBy.push_back(state.players().at(seat));
    }
    scoring.score = WireScore(state.score());
    view.scoring = std::move(scoring);
  }
  if (state.isOver()) view.result = WireResult(state);
  for (const go::GameScore& score : table->scoreSheet()) {
    moonbase::games::GoScoreLine line;
    line.winner = score.winner;
    line.ending = std::string(go::EndingName(score.ending));
    view.scoreSheet.push_back(std::move(line));
  }
  return view;
}

moonbase::games::GameEvents Adapter<go::Table>::StateEvent(View view) {
  moonbase::games::GoGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<go::Table>::JoinedEvent(View view) {
  moonbase::games::GoGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Each game past `told` off the score sheet. Only the last can still be
// the table's game, so earlier lines carry no score.
std::vector<moonbase::games::GameEvents> Adapter<go::Table>::GamesEnded(const go::Table& table,
                                                                        std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  const auto& sheet = table.scoreSheet();
  for (std::size_t i = told; i < sheet.size(); ++i) {
    moonbase::games::GoGameEnded ended;
    if (i + 1 == sheet.size() && table.game().isOver()) {
      ended.result = WireResult(table.game());
    } else {
      ended.result.winner = sheet[i].winner;
      if (sheet[i].winner_color.has_value()) {
        ended.result.winnerColor = std::string(go::ColorName(*sheet[i].winner_color));
      }
      ended.result.ending = std::string(go::EndingName(sheet[i].ending));
    }
    events.push_back(Wrap(Update::FromGameended(std::move(ended))));
  }
  return events;
}

Action<go::Table, Adapter<go::Table>::Start> Adapter<go::Table>::Decode(const Move& move,
                                                                        const std::string&) {
  using GoTransition = Transition<go::Table>;
  // startGame is go's own shape: it names the board, komi and clock. On a
  // table whose game ended, the next game; otherwise the first.
  if (const auto* start = move.as_startGame_or_null()) {
    return StartGame<Start>{TermsFrom(*start), /*next_game=*/true};
  }
  if (const auto* challenge = move.as_challenge_or_null()) {
    GoTerms terms = TermsFrom(*challenge);
    if (const absl::Status ok = go::CheckRules(terms.size, terms.komi, terms.time_control);
        !ok.ok()) {
      return Refusal{RejectKind::kRules, std::string(ok.message())};
    }
    return PostTerms{HostedTerms(std::move(terms))};
  }
  if (const auto* watch = move.as_watch_or_null()) return WatchTable{watch->gameId};
  if (const auto* create = move.as_createRoundRobin_or_null()) {
    GoTerms terms = TermsFrom(create->terms.value_or(moonbase::games::GoStartGame{}));
    if (const absl::Status ok = go::CheckRules(terms.size, terms.komi, terms.time_control);
        !ok.ok()) {
      return Refusal{RejectKind::kRules, std::string(ok.message())};
    }
    return CreateRoundRobin{create->entrants, HostedTerms(std::move(terms))};
  }
  if (const auto* withdraw = move.as_withdraw_or_null()) return Withdraw{*withdraw};
  if (const auto* forfeit = move.as_forfeit_or_null()) return Forfeit{*forfeit};
  if (move.as_roundRobins_or_null() != nullptr) return ListRoundRobins{};
  if (const auto* play = move.as_playRoundRobin_or_null()) return PlayRoundRobin{*play};
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  if (const auto* play = move.as_play_or_null()) {
    return GoTransition{
        [x = play->x, y = play->y](const go::Table& table, int seat, const MoveContext& context) {
          return table.inGame(
              [&](const go::GameState& game) { return game.play(seat, x, y, context.now_ms); });
        }};
  }
  if (move.as_pass_or_null() != nullptr) {
    return GoTransition{[](const go::Table& table, int seat, const MoveContext& context) {
      return table.inGame(
          [&](const go::GameState& game) { return game.pass(seat, context.now_ms); });
    }};
  }
  if (const auto* mark = move.as_markDead_or_null()) {
    return GoTransition{
        [x = mark->x, y = mark->y](const go::Table& table, int seat, const MoveContext& context) {
          return table.inGame(
              [&](const go::GameState& game) { return game.markDead(seat, x, y, context.now_ms); });
        }};
  }
  if (move.as_accept_or_null() != nullptr) {
    return GoTransition{[](const go::Table& table, int seat, const MoveContext& context) {
      return table.inGame(
          [&](const go::GameState& game) { return game.accept(seat, context.now_ms); });
    }};
  }
  if (move.as_resign_or_null() != nullptr) {
    return GoTransition{[](const go::Table& table, int seat, const MoveContext& context) {
      return table.inGame(
          [&](const go::GameState& game) { return game.resign(seat, context.now_ms); });
    }};
  }
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

absl::StatusOr<go::Table> Adapter<go::Table>::Open(const OpenContext& context, const Start& start,
                                                   std::deque<cards::Card>) {
  int black_seat = 0;
  if (context.pairing != nullptr) {
    const auto& roster = context.roster;
    black_seat = static_cast<int>(std::find(roster.begin(), roster.end(), context.pairing->black) -
                                  roster.begin());
  }
  return go::Table::open(context.roster, start.size, start.komi, start.time_control,
                         context.now_ms, black_seat);
}

moonbase::games::GameEvents Adapter<go::Table>::RoundRobinEvent(const HeldRoundRobin& held) {
  return Wrap(Update::FromRoundrobin(GoRoundRobinOf(held.row, held.live)));
}

moonbase::games::GameEvents Adapter<go::Table>::RoundRobinsEvent(
    const std::vector<HeldRoundRobin>& held) {
  moonbase::games::GoRoundRobins round_robins;
  for (const HeldRoundRobin& one : held) {
    round_robins.roundRobins.push_back(GoRoundRobinOf(one.row, one.live));
  }
  return Wrap(Update::FromRoundrobins(std::move(round_robins)));
}

nlohmann::json Adapter<go::Table>::TermsJson(const GoTerms& terms) {
  return {{"size", terms.size},
          {"komi", terms.komi},
          {"initialMs", terms.time_control.initial_ms},
          {"incrementMs", terms.time_control.increment_ms}};
}

std::optional<GoTerms> Adapter<go::Table>::TermsFromJson(const nlohmann::json& stored) {
  if (!stored.is_object() || !stored.contains("size") || !stored["size"].is_number_integer() ||
      !stored.contains("komi") || !stored["komi"].is_number() || !stored.contains("initialMs") ||
      !stored["initialMs"].is_number_integer() || !stored.contains("incrementMs") ||
      !stored["incrementMs"].is_number_integer()) {
    return std::nullopt;
  }
  GoTerms terms{stored["size"].get<int>(),
                stored["komi"].get<double>(),
                {stored["initialMs"].get<int64_t>(), stored["incrementMs"].get<int64_t>()}};
  if (!go::CheckRules(terms.size, terms.komi, terms.time_control).ok()) return std::nullopt;
  return terms;
}

std::string Adapter<go::Table>::TermsLine(const GoTerms& terms) {
  return absl::StrCat(terms.size, "×", terms.size, " · komi ", terms.komi, " · ",
                      ClockLine(terms.time_control.initial_ms, terms.time_control.increment_ms));
}

std::string Adapter<go::Table>::Serialize(const go::Table& engine) {
  return go::serializeTable(engine);
}

absl::StatusOr<go::Table> Adapter<go::Table>::Deserialize(const std::string& stored) {
  return go::deserializeTable(stored);
}

}  // namespace games_hub
