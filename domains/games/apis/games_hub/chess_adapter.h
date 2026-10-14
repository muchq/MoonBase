#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H

#include <array>
#include <functional>
#include <string_view>

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
  /// A game's terms, and the setup the hub's opener draws from them
  /// (Prepare); a start nothing prepared has none, and is refused.
  struct Start {
    ChessTerms terms{std::string(chess_play::kDefaultChessSetup), kDefaultChessClock};
    std::optional<chess_play::ChessSetup> setup;
  };
  static constexpr GameKind kKind = GameKind::kChess;
  static constexpr std::size_t kMaxSeats = chess_play::GameState::kSeats;
  static constexpr unsigned kCapabilities = kWatchable | kNextGame | kRoundRobins | kOpenings;
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
  static std::optional<StartGame<Start>> FilledStart(const std::optional<ChessTerms>& terms,
                                                     const std::optional<EventTag>&) {
    if (!terms.has_value()) return std::nullopt;
    return StartGame<Start>{Start{*terms, std::nullopt}};
  }
  static bool IsBot(std::string_view player_id) { return ChessBotElo(player_id).has_value(); }

  static absl::Status Prepare(Start& start, const ChessOpener& opener);
  static absl::StatusOr<chess_play::Table> Next(const chess_play::Table& table, const Start& start,
                                                const MoveContext& context) {
    return table.next(start.setup->variant, start.setup->opening, start.terms.time_control,
                      context.now_ms, start.setup->id);
  }
  // A round robin's table plays its one game on the round robin's terms.
  static void RoundRobinStart(Start& start, const std::optional<ChessTerms>& table_terms,
                              const HubStore::ChessEventRow*) {
    if (table_terms.has_value()) start.terms = *table_terms;
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ADAPTER_H
