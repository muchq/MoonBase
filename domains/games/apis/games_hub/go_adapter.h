#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GO_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GO_ADAPTER_H

#include <array>
#include <nlohmann/json.hpp>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/apis/games_hub/go_bots.h"
#include "domains/games/libs/go/table.h"

namespace games_hub {

template <>
struct Adapter<go::Table> {
  using Engine = go::Table;
  using Move = moonbase::games::GoMove;
  using Update = moonbase::games::GoUpdate;
  using View = moonbase::games::GoView;
  using Terms = GoTerms;
  /// The board, komi and clock a game plays on: a challenge's terms, or a
  /// startGame's.
  using Start = GoTerms;
  static constexpr GameKind kKind = GameKind::kGo;
  static constexpr std::string_view kName = "go";
  static constexpr std::size_t kMaxSeats = go::GameState::kSeats;
  static constexpr unsigned kCapabilities =
      kWatchable | kNextGame | kRoundRobins | kClocked | kChallenges | kEngineBots | kArchived;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "go_commands";
  static constexpr std::array<std::string_view, 17> kCommands = {
      "createGame", "joinGame", "startGame",   "leaveGame",      "play",      "pass",
      "resign",     "watch",    "markDead",    "accept",         "challenge", "createRoundRobin",
      "withdraw",   "forfeit",  "roundRobins", "playRoundRobin", "addBot"};
  static constexpr std::string_view kEventSeries = "go_events";
  static constexpr std::array<std::string_view, 9> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "turnChanged",
      "gameEnded",  "gameLeft",  "roundRobin",  "roundRobins"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_go_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_go_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const go::Table& table);
  static std::string Turn(const go::Table& table);
  static std::size_t GamesFinished(const go::Table& table) { return table.scoreSheet().size(); }
  static StatsDeltas ClosedDeltas(const go::Table& table, const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const go::Table& before, const go::Table& after);
  static bool DealStarted(const go::Table& before, const go::Table& after) {
    return before.game().isOver() && !after.game().isOver();
  }
  // As chess's: a game's end, the table playing on or closed by the leave
  // that ended it.
  static std::optional<GameEnd> Ended(const go::Table& table) {
    if (!table.game().isOver() || (table.isOver() && !table.endedByClose())) return std::nullopt;
    return table.game().result()->ending == go::Ending::kAbandoned ? GameEnd::kAbandoned
                                                                   : GameEnd::kCompleted;
  }
  // The leave reads the clock first: a flag already fallen ends the game
  // on time, not by the leave.
  static absl::StatusOr<go::Table> WithoutSeat(const go::Table& table, int seat, int64_t now_ms) {
    return table.removePlayer(seat, now_ms);
  }

  // Go hides nothing: every seat and watcher sees the one view.
  static View ViewOf(const TableContext& context, const go::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const go::Table& table,
                                                             std::size_t told);
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const go::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }

  // The clock is read as each move is applied, as chess's is.
  static Action<go::Table, Start> Decode(const Move& move, const std::string& player_id);
  // A pairing's table opens with the pairing's Black on move.
  static absl::StatusOr<go::Table> Open(const OpenContext& context, const Start& start,
                                        std::deque<cards::Card>);
  /// A challenge's table starts on its terms once its second seat fills.
  static std::optional<StartGame<Start>> FilledStart(const std::optional<HostedTerms>& terms,
                                                     const std::optional<EventTag>&) {
    if (!terms.has_value()) return std::nullopt;
    return StartGame<Start>{std::get<GoTerms>(*terms)};
  }
  static std::string Serialize(const go::Table& engine);
  static absl::StatusOr<go::Table> Deserialize(const std::string& stored);
  static bool IsBot(std::string_view player_id) { return GoBotLevel(player_id).has_value(); }

  using BotAsk = GoBotAsk;
  // The bot on turn, asked with the game so far; in the scoring, a bot
  // that has not accepted the marks, asked which stones are dead.
  static std::optional<BotTurn<GoBotAsk>> BotTurnOf(const go::Table& table, int64_t now_ms);
  // The answer, a GTP vertex, "pass" or "resign"; in the scoring, the
  // engine's dead stones, marked so as near as whole groups allow, then
  // accepted.
  static absl::StatusOr<go::Table> BotMove(const go::Table& table, int seat,
                                           std::string_view answer, const MoveContext& context);

  static absl::StatusOr<go::Table> Next(const go::Table& table, const Start& start,
                                        const MoveContext& context) {
    return table.next(start.size, start.komi, start.time_control, context.now_ms);
  }
  // Ended between games, or closed.
  static bool CurrentGameOver(const go::Table& table) { return table.game().isOver(); }

  // A round robin's table plays its one game on the round robin's terms,
  // which it waits on.
  static void RoundRobinStart(Start& start, const std::optional<HostedTerms>& table_terms,
                              const HubStore::ChessEventRow*) {
    if (table_terms.has_value()) start = std::get<GoTerms>(*table_terms);
  }
  using RoundRobins = moonbase::games::GoRoundRobins;
  static moonbase::games::GoRoundRobin RoundRobinWire(const HeldRoundRobin& held);
  // A go round robin's body names its game, and holds its terms as a
  // waiting table's state column does.
  static constexpr bool kRoundRobinBodyNamed = true;
  static nlohmann::json RoundRobinBody(const GoTerms& terms) {
    return {{"terms", TermsJson(terms)}};
  }
  static std::optional<GoTerms> FromRoundRobinBody(const nlohmann::json& body) {
    return body.contains("terms") ? TermsFromJson(body["terms"]) : std::nullopt;
  }
  // A tagged table plays its pairing's one game: once its sheet has a
  // line, that is the game, its winner off the sheet (none for a draw).
  static std::optional<HubStore::EventGame> TableGame(const go::Table& table) {
    if (table.scoreSheet().empty()) return std::nullopt;
    return HubStore::EventGame{0, "", 0, table.scoreSheet().front().winner};
  }

  // A table's game once it is over, on its line of the score sheet.
  static std::optional<HubStore::ArchivedGame> Archived(const go::Table& table) {
    if (!table.game().isOver()) return std::nullopt;
    return HubStore::GoGameRow{0, "", static_cast<int>(table.scoreSheet().size()), table.game(), 0};
  }

  // {"size", "komi", "initialMs", "incrementMs"}, the clock in integer
  // milliseconds; terms no game could open on read as nothing.
  static nlohmann::json TermsJson(const GoTerms& terms);
  static std::optional<GoTerms> TermsFromJson(const nlohmann::json& stored);
  // "9×9 · komi 5.5 · 1+0", the clock as ClockLine gives it.
  static std::string TermsLine(const GoTerms& terms);

  static bool FlagDue(const go::Table& table, int64_t now_ms) {
    const go::GameState& game = table.game();
    return !game.isOver() && game.remainingMs(game.sideToMove(), now_ms) <= 0;
  }
  static absl::StatusOr<go::Table> Flag(const go::Table& table, int64_t now_ms) {
    return table.inGame([now_ms](const go::GameState& game) { return game.flag(now_ms); });
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_GO_ADAPTER_H
