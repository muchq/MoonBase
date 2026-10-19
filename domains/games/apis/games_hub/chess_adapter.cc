#include "domains/games/apis/games_hub/chess_adapter.h"

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/chess_results.h"
#include "domains/games/apis/games_hub/round_robin_wire.h"
#include "domains/games/libs/chess_play/table_serde.h"

namespace games_hub {
namespace {

// Both seats played, the winner won, a draw credits nobody. A leaver
// played and lost.
StatsDeltas GameDeltas(const chess_play::GameState& state) {
  const std::optional<chess_play::Result>& result = state.result();
  std::string winner;
  if (result.has_value() && result->winner.has_value()) {
    winner = state.players().at(state.seatOf(*result->winner));
  }
  return PlayedWon(state.players(), winner);
}

}  // namespace

ChessTerms TermsFrom(const moonbase::games::ChessStartGame& fields) {
  ChessTerms terms{fields.setupId.value_or(std::string(chess_play::kDefaultChessSetup)),
                   kDefaultChessClock};
  if (fields.initialSeconds.has_value()) {
    terms.time_control.initial_ms = int64_t{*fields.initialSeconds} * 1000;
  }
  if (fields.incrementSeconds.has_value()) {
    terms.time_control.increment_ms = int64_t{*fields.incrementSeconds} * 1000;
  }
  return terms;
}

// A game in play, one ended with the table waiting on the next, or the
// table closed by a leave.
std::string Adapter<chess_play::Table>::Phase(const chess_play::Table& table) {
  if (table.isOver()) return "closed";
  return table.game().isOver() ? "ended" : "playing";
}

std::string Adapter<chess_play::Table>::Turn(const chess_play::Table& table) {
  const int seat = table.game().whoseTurn();
  return seat < 0 ? "" : table.players().at(seat);
}

// A table closes by a leave: one that ended a game credits it as any end
// does; one between games, nothing — each game was as it ended.
StatsDeltas Adapter<chess_play::Table>::ClosedDeltas(const chess_play::Table& table,
                                                     const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table.game()) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<chess_play::Table>::DealEndDeltas(
    const chess_play::Table& before, const chess_play::Table& after) {
  if (before.game().isOver() || !after.game().isOver() || after.isOver()) return std::nullopt;
  return GameDeltas(after.game());
}

moonbase::games::GameEvents Adapter<chess_play::Table>::Wrap(Update update) {
  moonbase::games::ChessEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromChess(std::move(event));
}

moonbase::games::ChessView Adapter<chess_play::Table>::ViewOf(const TableContext& context,
                                                              const chess_play::Table* table,
                                                              const std::string&) {
  moonbase::games::ChessView view;
  view.gameId = context.game_id;
  if (context.event.has_value()) view.roundRobinId = context.event->event_id;
  view.inCheck = false;
  view.defaultSetupId = chess_play::kDefaultChessSetup;
  for (const chess_play::ChessSetupOption& available : chess_play::AvailableChessSetups()) {
    moonbase::games::ChessSetupOption option;
    option.setupId = available.id;
    option.name = available.name;
    view.availableSetups.push_back(std::move(option));
  }
  if (table == nullptr) {
    view.phase = "waiting";
    if (context.terms.has_value()) view.terms = WireTerms(std::get<ChessTerms>(*context.terms));
    for (const std::string& roster_id : context.roster) {
      moonbase::games::ChessPlayer player;
      player.playerId = roster_id;
      if (ChessBotElo(roster_id).has_value()) player.bot = true;
      view.players.push_back(std::move(player));
    }
    return view;
  }
  const chess_play::GameState& state = table->game();
  const int64_t now = context.now_ms;
  view.phase = Phase(*table);
  view.variant = state.variant();
  view.setupId = state.setupId();
  view.setupName = std::string(*chess_play::ChessSetupName(state.setupId()));
  for (int seat = 0; seat < chess_play::GameState::kSeats; ++seat) {
    moonbase::games::ChessPlayer player;
    player.playerId = state.players().at(seat);
    player.color = std::string(chess_play::ColorName(state.colorOf(seat)));
    if (ChessBotElo(player.playerId).has_value()) player.bot = true;
    view.players.push_back(std::move(player));
  }
  view.fen = state.fen();
  view.moves = state.moves();
  if (!state.isOver()) {
    view.sideToMove = std::string(chess_play::ColorName(state.sideToMove()));
    view.currentPlayerId = state.players().at(state.whoseTurn());
  }
  view.inCheck = state.inCheck();
  view.legalMoves = state.legalMoves();
  moonbase::games::ChessClock clock;
  clock.whiteMs = state.remainingMs(chess_play::Color::kWhite, now);
  clock.blackMs = state.remainingMs(chess_play::Color::kBlack, now);
  clock.initialMs = state.timeControl().initial_ms;
  clock.incrementMs = state.timeControl().increment_ms;
  view.clock = clock;
  if (state.isOver()) view.result = WireChessResult(chess_play::ScoreOf(state));
  for (const chess_play::GameScore& score : table->scoreSheet()) {
    moonbase::games::ChessScoreLine line;
    line.winner = score.winner;
    line.ending = std::string(chess_play::EndingName(score.ending));
    view.scoreSheet.push_back(std::move(line));
  }
  return view;
}

moonbase::games::GameEvents Adapter<chess_play::Table>::StateEvent(View view) {
  moonbase::games::ChessGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<chess_play::Table>::JoinedEvent(View view) {
  moonbase::games::ChessGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Read from the score sheet, so a game that ended before the next began
// is owed all the same.
std::vector<moonbase::games::GameEvents> Adapter<chess_play::Table>::GamesEnded(
    const chess_play::Table& table, std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  for (moonbase::games::ChessResult& result : ChessResultsSince(table, told)) {
    moonbase::games::ChessGameEnded ended;
    ended.result = std::move(result);
    events.push_back(Wrap(Update::FromGameended(std::move(ended))));
  }
  return events;
}

Action<chess_play::Table, Adapter<chess_play::Table>::Start> Adapter<chess_play::Table>::Decode(
    const Move& move, const std::string&) {
  using ChessTransition = Transition<chess_play::Table>;
  // startGame is chess's own shape: it names the clock. On a table whose
  // game ended, the next game; otherwise the first.
  if (const auto* start = move.as_startGame_or_null()) {
    return StartGame<Start>{Start{TermsFrom(*start), std::nullopt}, /*next_game=*/true};
  }
  if (const auto* add = move.as_addBot_or_null()) return AddBot{ChessBotId(add->elo), true};
  if (const auto* watch = move.as_watch_or_null()) return WatchTable{watch->gameId};
  if (move.as_history_or_null() != nullptr) return AskHistory{};
  if (const auto* review = move.as_review_or_null()) return AskReview{*review};
  if (const auto* publish = move.as_publish_or_null()) return Publish{publish->published};
  if (const auto* challenge = move.as_challenge_or_null()) {
    ChessTerms terms = TermsFrom(*challenge);
    if (!chess_play::ChessSetupName(terms.setup_id).has_value()) {
      return Refusal{RejectKind::kRules, absl::StrCat("unknown chess setup: ", terms.setup_id)};
    }
    return PostTerms{HostedTerms(std::move(terms))};
  }
  if (const auto* create = move.as_createRoundRobin_or_null()) {
    ChessTerms terms = TermsFrom(create->terms.value_or(moonbase::games::ChessStartGame{}));
    if (!chess_play::ChessSetupName(terms.setup_id).has_value()) {
      return Refusal{RejectKind::kRules, absl::StrCat("unknown chess setup: ", terms.setup_id)};
    }
    return CreateRoundRobin{create->entrants, HostedTerms(std::move(terms))};
  }
  if (auto round_robin = RoundRobinMove<chess_play::Table, Start>(move))
    return *std::move(round_robin);
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  if (const auto* play = move.as_play_or_null()) {
    return ChessTransition{
        [uci = play->uci](const chess_play::Table& table, int seat, const MoveContext& context) {
          return table.inGame([&](const chess_play::GameState& game) {
            return game.move(seat, uci, context.now_ms);
          });
        }};
  }
  if (move.as_resign_or_null() != nullptr) {
    return ChessTransition{
        [](const chess_play::Table& table, int seat, const MoveContext& context) {
          return table.inGame(
              [&](const chess_play::GameState& game) { return game.resign(seat, context.now_ms); });
        }};
  }
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

// A pairing's table seats its white as the round robin drew it.
absl::StatusOr<chess_play::Table> Adapter<chess_play::Table>::Open(const OpenContext& context,
                                                                   const Start& start,
                                                                   std::deque<cards::Card>) {
  if (!start.setup.has_value()) return absl::InvalidArgumentError("a chess setup is required");
  chess_play::Opening opening = start.setup->opening;
  if (context.pairing != nullptr) {
    const auto& roster = context.roster;
    opening.white_seat = static_cast<int>(
        std::find(roster.begin(), roster.end(), context.pairing->white) - roster.begin());
  }
  return chess_play::Table::open(context.roster, start.setup->variant, opening,
                                 start.terms.time_control, context.now_ms, start.setup->id);
}

nlohmann::json Adapter<chess_play::Table>::TermsJson(const ChessTerms& terms) {
  return {{"setupId", terms.setup_id},
          {"initialMs", terms.time_control.initial_ms},
          {"incrementMs", terms.time_control.increment_ms}};
}

std::optional<ChessTerms> Adapter<chess_play::Table>::TermsFromJson(const nlohmann::json& stored) {
  if (!stored.is_object() || !stored.contains("setupId") || !stored["setupId"].is_string() ||
      !stored.contains("initialMs") || !stored["initialMs"].is_number_integer() ||
      !stored.contains("incrementMs") || !stored["incrementMs"].is_number_integer()) {
    return std::nullopt;
  }
  return ChessTerms{stored["setupId"].get<std::string>(),
                    {stored["initialMs"].get<int64_t>(), stored["incrementMs"].get<int64_t>()}};
}

std::string Adapter<chess_play::Table>::TermsLine(const ChessTerms& terms) {
  return absl::StrCat(chess_play::ChessSetupName(terms.setup_id).value_or(terms.setup_id), " · ",
                      ClockLine(terms.time_control.initial_ms, terms.time_control.increment_ms));
}

absl::Status Adapter<chess_play::Table>::Prepare(Start& start, const ChessOpener& opener) {
  auto setup = opener(start.terms.setup_id);
  if (!setup.ok()) return setup.status();
  start.setup = *std::move(setup);
  return absl::OkStatus();
}

moonbase::games::ChessRoundRobin Adapter<chess_play::Table>::RoundRobinWire(
    const HeldRoundRobin& held) {
  return RoundRobinOf(held.row, held.live);
}

std::optional<BotTurn<ChessBotAsk>> Adapter<chess_play::Table>::BotTurnOf(
    const chess_play::Table& table, int64_t now_ms) {
  const chess_play::GameState& game = table.game();
  const int seat = game.whoseTurn();
  if (seat < 0) return std::nullopt;
  const std::string& on_turn = game.players().at(seat);
  const std::optional<int> elo = ChessBotElo(on_turn);
  if (!elo.has_value()) return std::nullopt;
  return BotTurn<ChessBotAsk>{
      on_turn, table.scoreSheet().size(), absl::StrCat(game.startFen(), " ", game.moves().size()),
      ChessBotAsk{game.startFen(), game.moves(),
                  ChessBotMovetimeMs(game.remainingMs(game.sideToMove(), now_ms), *elo),
                  std::nullopt}};
}

absl::StatusOr<chess_play::Table> Adapter<chess_play::Table>::BotMove(
    const chess_play::Table& table, int seat, std::string_view answer, const MoveContext& context) {
  return table.inGame([&](const chess_play::GameState& state) {
    return state.move(seat, std::string(answer), context.now_ms);
  });
}

std::string Adapter<chess_play::Table>::Serialize(const chess_play::Table& engine) {
  return chess_play::serializeTable(engine);
}

absl::StatusOr<chess_play::Table> Adapter<chess_play::Table>::Deserialize(
    const std::string& stored) {
  return chess_play::deserializeTable(stored);
}

}  // namespace games_hub
