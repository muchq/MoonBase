#include "domains/games/apis/games_hub/go_adapter.h"

#include <algorithm>
#include <set>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "domains/games/apis/games_hub/round_robin_wire.h"
#include "domains/games/libs/go/sgf.h"
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
      if (IsBot(roster_id)) player.bot = true;
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
    if (IsBot(player.playerId)) player.bot = true;
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
  if (const auto* add = move.as_addBot_or_null()) return AddBot{GoBotId(add->level), true};
  if (const auto* create = move.as_createRoundRobin_or_null()) {
    GoTerms terms = TermsFrom(create->terms.value_or(moonbase::games::GoStartGame{}));
    if (const absl::Status ok = go::CheckRules(terms.size, terms.komi, terms.time_control);
        !ok.ok()) {
      return Refusal{RejectKind::kRules, std::string(ok.message())};
    }
    return CreateRoundRobin{create->entrants, HostedTerms(std::move(terms))};
  }
  if (auto round_robin = RoundRobinMove<go::Table, Start>(move)) return *std::move(round_robin);
  if (move.as_history_or_null() != nullptr) return AskHistory{};
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

namespace {

GoBotAsk AskOf(const go::GameState& game, int level, bool scoring) {
  GoBotAsk ask{game.size(), game.komi(), {}, level, scoring};
  for (const int point : game.moves()) ask.moves.push_back(GtpVertex(point, game.size()));
  return ask;
}

// How many points the marks and the engine's dead stones disagree on.
std::size_t Disagreement(const go::GameState& game, const std::set<int>& dead) {
  std::size_t apart = 0;
  for (const int point : game.dead()) apart += dead.contains(point) ? 0 : 1;
  for (const int point : dead) {
    apart += game.isDead(point % game.size(), point / game.size()) ? 0 : 1;
  }
  return apart;
}

// The marks moved toward the engine's dead stones a group at a time, while
// each toggle brings them closer (a group is marked whole, so an engine
// naming part of one is met as near as can be), then accepted.
absl::StatusOr<go::GameState> MarkedAndAccepted(const go::GameState& game, int seat,
                                                std::string_view answer, int64_t now_ms) {
  std::set<int> dead;
  for (const std::string_view vertex : absl::StrSplit(answer, ' ', absl::SkipEmpty())) {
    const std::optional<int> point = GtpPoint(vertex, game.size());
    if (!point.has_value() || *point == go::kPass) {
      return absl::InvalidArgumentError(absl::StrCat("not a dead stone: ", vertex));
    }
    dead.insert(*point);
  }
  go::GameState marked = game;
  for (bool closer = true; closer;) {
    closer = false;
    std::set<int> apart = dead;
    for (const int point : marked.dead()) {
      if (apart.erase(point) == 0) apart.insert(point);
    }
    for (const int point : apart) {
      auto toggled = marked.markDead(seat, point % game.size(), point / game.size(), now_ms);
      if (!toggled.ok() || Disagreement(*toggled, dead) >= Disagreement(marked, dead)) continue;
      marked = *std::move(toggled);
      closer = true;
      break;
    }
  }
  return marked.accept(seat, now_ms);
}

}  // namespace

std::optional<BotTurn<GoBotAsk>> Adapter<go::Table>::BotTurnOf(const go::Table& table, int64_t) {
  const go::GameState& game = table.game();
  if (table.isOver() || game.isOver()) return std::nullopt;
  const std::size_t game_no = table.scoreSheet().size();
  if (game.scoring()) {
    for (int seat = 0; seat < go::GameState::kSeats; ++seat) {
      const std::string& id = game.players().at(seat);
      const std::optional<int> level = GoBotLevel(id);
      if (!level.has_value() || game.accepted()[seat]) continue;
      return BotTurn<GoBotAsk>{
          id, game_no,
          absl::StrCat("scoring ", game.moves().size(), " ", absl::StrJoin(game.dead(), ","), " ",
                       game.accepted()[0], game.accepted()[1]),
          AskOf(game, *level, /*scoring=*/true)};
    }
    return std::nullopt;
  }
  const int seat = game.whoseTurn();
  if (seat < 0) return std::nullopt;
  const std::string& on_turn = game.players().at(seat);
  const std::optional<int> level = GoBotLevel(on_turn);
  if (!level.has_value()) return std::nullopt;
  return BotTurn<GoBotAsk>{on_turn, game_no, absl::StrCat(game.moves().size()),
                           AskOf(game, *level, /*scoring=*/false)};
}

absl::StatusOr<go::Table> Adapter<go::Table>::BotMove(const go::Table& table, int seat,
                                                      std::string_view answer,
                                                      const MoveContext& context) {
  answer = absl::StripAsciiWhitespace(answer);
  return table.inGame([&](const go::GameState& game) -> absl::StatusOr<go::GameState> {
    if (game.scoring()) return MarkedAndAccepted(game, seat, answer, context.now_ms);
    if (absl::EqualsIgnoreCase(answer, "resign")) return game.resign(seat, context.now_ms);
    const std::optional<int> point = GtpPoint(answer, game.size());
    if (!point.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("not a vertex: ", answer));
    }
    if (*point == go::kPass) return game.pass(seat, context.now_ms);
    return game.play(seat, *point % game.size(), *point / game.size(), context.now_ms);
  });
}

moonbase::games::GoRoundRobin Adapter<go::Table>::RoundRobinWire(const HeldRoundRobin& held) {
  return GoRoundRobinOf(held.row, held.live);
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

moonbase::games::GoHistory GoHistoryOf(const std::vector<HubStore::GoGameRow>& games) {
  moonbase::games::GoHistory history;
  for (const HubStore::GoGameRow& row : games) {
    const go::GameState& game = row.game;
    moonbase::games::GoGameSummary summary;
    summary.archiveId = row.archive_id;
    summary.gameId = row.game_id;
    summary.ordinal = row.ordinal;
    summary.black = game.players().at(game.seatOf(go::Color::kBlack));
    summary.white = game.players().at(game.seatOf(go::Color::kWhite));
    summary.size = game.size();
    summary.komi = game.komi();
    summary.result = WireResult(game);
    summary.endedAtMs = row.ended_at_ms;
    summary.sgf = go::ToSgf(game, row.ended_at_ms);
    history.games.push_back(std::move(summary));
  }
  return history;
}

}  // namespace games_hub
