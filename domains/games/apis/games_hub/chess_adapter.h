#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H

#include <array>
#include <functional>
#include <string_view>

#include <nlohmann/json.hpp>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/apis/games_hub/chess_bots.h"

namespace games_hub {

/// A chess clock the starter did not name: three minutes and two seconds.
inline constexpr chess_play::TimeControl kDefaultChessClock{180'000, 2'000};

/// Where a chess table starts, by the setup its terms name: the hub's,
/// which tests fix.
using ChessOpener =
    std::function<absl::StatusOr<chess_play::ChessSetup>(std::string_view setup_id)>;

/// A startGame's or a challenge's fields, absent ones taking the defaults:
/// the standard position, three minutes and two seconds.
ChessTerms TermsFrom(const moonbase::games::ChessStartGame& fields);

template <>
struct Adapter<chess_play::Table> {
  using Engine = chess_play::Table;
  using Move = moonbase::games::ChessMove;
  using Update = moonbase::games::ChessUpdate;
  using View = moonbase::games::ChessView;
  using Terms = ChessTerms;
  /// A game's terms, and the setup the hub's opener draws from them
  /// (Prepare); a start nothing prepared has none, and is refused.
  struct Start {
    ChessTerms terms{std::string(chess_play::kDefaultChessSetup), kDefaultChessClock};
    std::optional<chess_play::ChessSetup> setup;
  };
  static constexpr GameKind kKind = GameKind::kChess;
  static constexpr std::string_view kName = "chess";
  static constexpr std::size_t kMaxSeats = chess_play::GameState::kSeats;
  static constexpr unsigned kCapabilities = kWatchable | kNextGame | kRoundRobins | kOpenings |
                                            kClocked | kEngineBots | kPublished | kChallenges;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "chess_commands";
  static constexpr std::array<std::string_view, 17> kCommands = {
      "createGame",       "joinGame", "startGame", "leaveGame",   "play",          "resign",
      "addBot",           "watch",    "history",   "review",      "publish",       "challenge",
      "createRoundRobin", "withdraw", "forfeit",   "roundRobins", "playRoundRobin"};
  static constexpr std::string_view kEventSeries = "chess_events";
  static constexpr std::array<std::string_view, 12> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "turnChanged", "gameEnded",
      "gameLeft",   "history",   "review",      "published",   "roundRobin",  "roundRobins"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_chess_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_chess_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const chess_play::Table& table);
  static std::string Turn(const chess_play::Table& table);
  static std::size_t GamesFinished(const chess_play::Table& table) {
    return table.scoreSheet().size();
  }
  static StatsDeltas ClosedDeltas(const chess_play::Table& table,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const chess_play::Table& before,
                                                  const chess_play::Table& after);
  static bool DealStarted(const chess_play::Table& before, const chess_play::Table& after) {
    return before.game().isOver() && !after.game().isOver();
  }
  // A game's end, the table playing on or closed by the leave that ended
  // it. A table closed between games recorded its last when it ended.
  static std::optional<GameEnd> Ended(const chess_play::Table& table) {
    if (!table.game().isOver() || (table.isOver() && !table.endedByClose())) return std::nullopt;
    return table.game().result()->ending == chess_play::Ending::kAbandoned ? GameEnd::kAbandoned
                                                                           : GameEnd::kCompleted;
  }
  // The leave reads the clock first: a flag already fallen ends the game
  // on time, not by the leave.
  static absl::StatusOr<chess_play::Table> WithoutSeat(const chess_play::Table& table, int seat,
                                                       int64_t now_ms) {
    return table.removePlayer(seat, now_ms);
  }

  // Chess hides nothing: every seat and watcher sees the one view.
  static View ViewOf(const TableContext& context, const chess_play::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const chess_play::Table& table,
                                                             std::size_t told);
  // Each game's result was its own gameEnded, the close's among them.
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const chess_play::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }

  // The clock is read as each move is applied, so a move that waited on
  // the hub's lock is charged from when it landed.
  static Action<chess_play::Table, Start> Decode(const Move& move, const std::string& player_id);
  static absl::StatusOr<chess_play::Table> Open(const OpenContext& context, const Start& start,
                                                std::deque<cards::Card>);
  /// A challenge's table starts on its terms once its second seat fills.
  static std::optional<StartGame<Start>> FilledStart(const std::optional<HostedTerms>& terms,
                                                     const std::optional<EventTag>&) {
    if (!terms.has_value()) return std::nullopt;
    return StartGame<Start>{Start{std::get<ChessTerms>(*terms), std::nullopt}};
  }
  static std::string Serialize(const chess_play::Table& engine);
  static absl::StatusOr<chess_play::Table> Deserialize(const std::string& stored);
  static bool IsBot(std::string_view player_id) { return ChessBotElo(player_id).has_value(); }

  using BotAsk = ChessBotAsk;
  // The bot on turn, asked to think for its named strength's time, capped
  // by its side's clock at `now_ms`; Elo is not sent (full strength).
  static std::optional<BotTurn<ChessBotAsk>> BotTurnOf(const chess_play::Table& table,
                                                       int64_t now_ms);
  // The answer, a UCI move.
  static absl::StatusOr<chess_play::Table> BotMove(const chess_play::Table& table, int seat,
                                                   std::string_view answer,
                                                   const MoveContext& context);

  static absl::Status Prepare(Start& start, const ChessOpener& opener);
  static absl::StatusOr<chess_play::Table> Next(const chess_play::Table& table, const Start& start,
                                                const MoveContext& context) {
    return table.next(start.setup->variant, start.setup->opening, start.terms.time_control,
                      context.now_ms, start.setup->id);
  }
  // Ended between games, or closed.
  static bool CurrentGameOver(const chess_play::Table& table) { return table.game().isOver(); }

  // A round robin's table plays its one game on the round robin's terms,
  // which it waits on.
  static void RoundRobinStart(Start& start, const std::optional<HostedTerms>& table_terms,
                              const HubStore::ChessEventRow*) {
    if (table_terms.has_value()) start.terms = std::get<ChessTerms>(*table_terms);
  }
  using RoundRobins = moonbase::games::ChessRoundRobins;
  static moonbase::games::ChessRoundRobin RoundRobinWire(const HeldRoundRobin& held);
  // A chess round robin's body names no game, and holds its terms as a
  // waiting table's state column does.
  static constexpr bool kRoundRobinBodyNamed = false;
  static nlohmann::json RoundRobinBody(const ChessTerms& terms) {
    return {{"terms", TermsJson(terms)}};
  }
  static std::optional<ChessTerms> FromRoundRobinBody(const nlohmann::json& body) {
    return body.contains("terms") ? TermsFromJson(body["terms"]) : std::nullopt;
  }

  // {"setupId", "initialMs", "incrementMs"}, the clock in integer
  // milliseconds.
  static nlohmann::json TermsJson(const ChessTerms& terms);
  static std::optional<ChessTerms> TermsFromJson(const nlohmann::json& stored);
  // "Standard starting position · 3+2", the clock as ClockLine gives it.
  static std::string TermsLine(const ChessTerms& terms);
  // A pairing's games are the archived ones its table played (Archived).
  static std::optional<HubStore::EventGame> TableGame(const chess_play::Table&) {
    return std::nullopt;
  }
  // A table's game once it is over, on its line of the score sheet.
  static std::optional<HubStore::ChessGameRow> Archived(const chess_play::Table& table) {
    if (!table.game().isOver()) return std::nullopt;
    return HubStore::ChessGameRow{
        0, "", static_cast<int>(table.scoreSheet().size()), table.game(), 0, false};
  }

  static bool FlagDue(const chess_play::Table& table, int64_t now_ms) {
    const chess_play::GameState& game = table.game();
    return !game.isOver() && game.remainingMs(game.sideToMove(), now_ms) <= 0;
  }
  static absl::StatusOr<chess_play::Table> Flag(const chess_play::Table& table, int64_t now_ms) {
    return table.inGame([now_ms](const chess_play::GameState& game) { return game.flag(now_ms); });
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
