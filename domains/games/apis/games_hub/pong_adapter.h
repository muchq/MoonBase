#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H

#include <array>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/apis/games_hub/pong_bots.h"

namespace games_hub {

template <>
struct Adapter<pong::Table> {
  using Engine = pong::Table;
  using Move = moonbase::games::PongMove;
  using Update = moonbase::games::PongUpdate;
  using View = moonbase::games::PongView;
  /// The variant a game plays.
  struct Start {
    pong::Variant variant = pong::Variant::kSmallcat;
  };
  static constexpr GameKind kKind = GameKind::kPong;
  static constexpr std::string_view kName = "pong";
  static constexpr std::size_t kMaxSeats = pong::kSeats;
  static constexpr unsigned kCapabilities = kWatchable | kNextGame | kRoundRobins | kRealtime;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "pong_commands";
  static constexpr std::array<std::string_view, 12> kCommands = {
      "createGame", "joinGame",         "startGame", "leaveGame", "paddle",      "addBot",
      "watch",      "createRoundRobin", "withdraw",  "forfeit",   "roundRobins", "playRoundRobin"};
  static constexpr std::string_view kEventSeries = "pong_events";
  static constexpr std::array<std::string_view, 9> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "gameEnded",
      "gameLeft",   "frame",     "roundRobin",  "roundRobins"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_pong_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_pong_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const pong::Table& table);
  // Pong has no turns: both seats play at once.
  static std::string Turn(const pong::Table&) { return ""; }
  static std::size_t GamesFinished(const pong::Table& table) { return table.scoreSheet().size(); }
  static StatsDeltas ClosedDeltas(const pong::Table& table, const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const pong::Table& before,
                                                  const pong::Table& after);
  static bool DealStarted(const pong::Table& before, const pong::Table& after) {
    return !before.inPlay() && after.inPlay();
  }
  // As chess's: a game's end, the table playing on or closed by the leave
  // that ended it.
  static std::optional<GameEnd> Ended(const pong::Table& table) {
    if (table.inPlay() || table.scoreSheet().empty() || (table.isOver() && !table.endedByClose())) {
      return std::nullopt;
    }
    return table.scoreSheet().back().ending == pong::Ending::kAbandoned ? GameEnd::kAbandoned
                                                                        : GameEnd::kCompleted;
  }
  static absl::StatusOr<pong::Table> WithoutSeat(const pong::Table& table, int seat, int64_t) {
    return table.removePlayer(seat);
  }

  // Pong hides nothing either: every seat and watcher sees the one view.
  static View ViewOf(const TableContext& context, const pong::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const pong::Table& table,
                                                             std::size_t told);
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const pong::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }

  // Its own startGame, a seat's paddle, a bot to the second seat, and
  // watching.
  static Action<pong::Table, Start> Decode(const Move& move, const std::string& player_id);
  static bool Steers(const Move& move) { return move.as_paddle_or_null() != nullptr; }
  static absl::StatusOr<pong::Table> Open(const OpenContext& context, const Start& start,
                                          std::deque<cards::Card>) {
    return pong::Table::open(context.roster, start.variant);
  }
  /// A pairing's table starts when it fills, on the round robin's variant.
  static std::optional<StartGame<Start>> FilledStart(const std::optional<ChessTerms>&,
                                                     const std::optional<EventTag>& event) {
    if (!event.has_value()) return std::nullopt;
    return StartGame<Start>{Start{}, /*next_game=*/true};
  }
  static std::string Serialize(const pong::Table& engine);
  static absl::StatusOr<pong::Table> Deserialize(const std::string& stored);
  static bool IsBot(std::string_view player_id) { return PongBotLevel(player_id).has_value(); }

  static absl::StatusOr<pong::Table> Next(const pong::Table& table, const Start& start,
                                          const MoveContext&) {
    return table.next(start.variant);
  }
  // Ended with the table waiting on the next, or closed.
  static bool CurrentGameOver(const pong::Table& table) { return !table.inPlay(); }

  // A round robin's table plays its one game on the round robin's variant.
  static void RoundRobinStart(Start& start, const std::optional<ChessTerms>&,
                              const HubStore::ChessEventRow* row) {
    if (row != nullptr) start.variant = row->pong_variant;
  }
  // A pairing's table starts on the round robin's variant when it fills,
  // and waits on no terms.
  static std::optional<ChessTerms> RoundRobinTerms(const HubStore::ChessEventRow&) {
    return std::nullopt;
  }
  static moonbase::games::GameEvents RoundRobinEvent(const HeldRoundRobin& held);
  static moonbase::games::GameEvents RoundRobinsEvent(const std::vector<HeldRoundRobin>& held);
  // A pong round robin's body names its game and the variant its pairings
  // play.
  static constexpr bool kRoundRobinBodyNamed = true;
  static std::vector<std::pair<std::string, std::string>> RoundRobinBody(
      const HubStore::ChessEventRow& row) {
    return {{"variant", std::string(pong::VariantName(row.pong_variant))}};
  }
  static bool FromRoundRobinBody(const std::map<std::string, std::string>& fields,
                                 HubStore::ChessEventRow& row) {
    const auto variant = fields.find("variant");
    if (variant == fields.end()) return false;
    const std::optional<pong::Variant> parsed = pong::ParseVariant(variant->second);
    if (!parsed.has_value()) return false;
    row.pong_variant = *parsed;
    return true;
  }
  // A tagged table plays its pairing's one game: once its sheet has a
  // line, that is the game, its winner off the sheet.
  static std::optional<HubStore::EventGame> TableGame(const pong::Table& table) {
    if (table.scoreSheet().empty()) return std::nullopt;
    return HubStore::EventGame{0, "", 0, table.scoreSheet().front().winner};
  }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_PONG_ADAPTER_H
