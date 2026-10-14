#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H

#include <array>
#include <unordered_set>

#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

/// winners() restricted to the seats the roster still names — the
/// forfeit rule. A final state can carry a seat its roster has dropped:
/// an abandonment that ended the game keeps the departed hand so its
/// cards and score reach the scorecard (#1236), and such a seat cannot
/// win — however good its standing score, the game resolved against it.
/// For an ordinary finish the roster names every seat and this is
/// exactly the engine's rule, knocker-takes-ties included.
[[nodiscard]] std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                                   const std::vector<std::string>& roster);

/// Seats are integer indexes; the occupant's identity is the player id.
std::string PlayerIdAt(const golf::GameState& state, int seat);

template <>
struct Adapter<golf::GameState> {
  using Engine = golf::GameState;
  using Move = moonbase::games::GolfMove;
  using Update = moonbase::games::GolfUpdate;
  using View = moonbase::games::GameView;
  struct Start {};
  static constexpr GameKind kKind = GameKind::kGolf;
  static constexpr std::size_t kMaxSeats = 4;
  static constexpr unsigned kCapabilities = 0;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "golf_commands";
  static constexpr std::array<std::string_view, 11> kCommands = {
      "createGame",      "joinGame", "startGame",    "leaveGame", "peekCard", "drawCard",
      "takeFromDiscard", "swapCard", "discardDrawn", "knock",     "hideCards"};
  static constexpr std::string_view kEventSeries = "golf_events";
  static constexpr std::array<std::string_view, 8> kEvents = {
      "gameJoined",  "gameState",     "gameCreated", "gameStarted",
      "turnChanged", "playerKnocked", "gameEnded",   "gameLeft"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_golf_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_golf_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const golf::GameState& state);
  static std::string Turn(const golf::GameState& state);
  static std::size_t GamesFinished(const golf::GameState&) { return 0; }
  static StatsDeltas ClosedDeltas(const golf::GameState& state,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const golf::GameState&, const golf::GameState&) {
    return std::nullopt;
  }
  static bool DealStarted(const golf::GameState&, const golf::GameState&) { return false; }
  static absl::StatusOr<golf::GameState> WithoutSeat(const golf::GameState& state, int seat,
                                                     int64_t) {
    return state.removePlayer(seat);
  }

  // Own peeks and the held draw ride only to their seat; every card
  // shows once the game ends.
  static View ViewOf(const TableContext& context, const golf::GameState* state,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const golf::GameState&, std::size_t) {
    return {};
  }
  static std::optional<moonbase::games::GameEvents> ClosedEvent(
      const golf::GameState& state, const std::vector<std::string>& roster);

  static Action<golf::GameState, Start> Decode(const Move& move, const std::string& player_id);
  static absl::StatusOr<golf::GameState> Open(const OpenContext& context, const Start&,
                                              std::deque<cards::Card> deck) {
    return golf::dealGolfGame(context.game_id, context.roster, std::move(deck));
  }
  static std::optional<StartGame<Start>> FilledStart(const std::optional<ChessTerms>&,
                                                     const std::optional<EventTag>&) {
    return std::nullopt;
  }
  static bool IsBot(std::string_view) { return false; }
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_ADAPTER_H
