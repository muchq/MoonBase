#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GO_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GO_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/libs/go/table.h"

namespace games_hub {

/// A go game the starter did not name: 19 by 19, komi 6.5, ten minutes
/// and five seconds.
inline constexpr int kDefaultGoSize = 19;
inline constexpr go::TimeControl kDefaultGoClock{600'000, 5'000};

template <>
struct Adapter<go::Table> {
  using Engine = go::Table;
  using Move = moonbase::games::GoMove;
  using Update = moonbase::games::GoUpdate;
  using View = moonbase::games::GoView;
  /// The board, komi and clock a game plays on.
  struct Start {
    int size = kDefaultGoSize;
    double komi = go::kDefaultKomi;
    go::TimeControl time_control = kDefaultGoClock;
  };
  static constexpr GameKind kKind = GameKind::kGo;
  static constexpr std::string_view kName = "go";
  static constexpr std::size_t kMaxSeats = go::GameState::kSeats;
  static constexpr unsigned kCapabilities = kWatchable | kNextGame | kClocked;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "go_commands";
  static constexpr std::array<std::string_view, 8> kCommands = {
      "createGame", "joinGame", "startGame", "leaveGame", "play", "pass", "resign", "watch"};
  static constexpr std::string_view kEventSeries = "go_events";
  static constexpr std::array<std::string_view, 7> kEvents = {
      "gameJoined",  "gameState", "gameCreated", "gameStarted",
      "turnChanged", "gameEnded", "gameLeft"};
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
  static absl::StatusOr<go::Table> Open(const OpenContext& context, const Start& start,
                                        std::deque<cards::Card>) {
    return go::Table::open(context.roster, start.size, start.komi, start.time_control,
                           context.now_ms);
  }
  static std::optional<StartGame<Start>> FilledStart(const std::optional<HostedTerms>&,
                                                     const std::optional<EventTag>&) {
    return std::nullopt;
  }
  static std::string Serialize(const go::Table& engine);
  static absl::StatusOr<go::Table> Deserialize(const std::string& stored);
  static bool IsBot(std::string_view) { return false; }

  static absl::StatusOr<go::Table> Next(const go::Table& table, const Start& start,
                                        const MoveContext& context) {
    return table.next(start.size, start.komi, start.time_control, context.now_ms);
  }
  // Ended between games, or closed.
  static bool CurrentGameOver(const go::Table& table) { return table.game().isOver(); }

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
