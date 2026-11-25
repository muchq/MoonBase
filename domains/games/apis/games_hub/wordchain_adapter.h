#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WORDCHAIN_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WORDCHAIN_ADAPTER_H

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/libs/wordchain_race/table.h"

namespace games_hub {

template <>
struct Adapter<wordchain_race::Table> {
  using Engine = wordchain_race::Table;
  using Move = moonbase::games::WordchainMove;
  using Update = moonbase::games::WordchainUpdate;
  using View = moonbase::games::WordchainView;
  /// A quick race's seek.
  using Terms = WordchainTerms;
  /// The race a startGame names; a quick race's counts down first.
  struct Start {
    int letters = 4;
    int64_t duration_ms = 180'000;
    int64_t start_delay_ms = 0;
  };
  static constexpr GameKind kKind = GameKind::kWordchain;
  static constexpr std::string_view kName = "wordchain";
  static constexpr std::size_t kMaxSeats = wordchain_race::kMaxRunners;
  // One seat races alone, for practice.
  static constexpr std::size_t kMinSeats = 1;
  static constexpr unsigned kCapabilities = kNextGame | kClocked | kChallenges;
  static constexpr bool kOpenStartsAGame = true;

  static constexpr std::string_view kCommandSeries = "wordchain_commands";
  static constexpr std::array<std::string_view, 7> kCommands = {
      "createGame", "joinGame", "startGame", "leaveGame", "play", "retract", "seek"};
  static constexpr std::string_view kEventSeries = "wordchain_events";
  static constexpr std::array<std::string_view, 6> kEvents = {
      "gameJoined", "gameState", "gameCreated", "gameStarted", "gameEnded", "gameLeft"};
  static const Move* MoveOf(const moonbase::games::GameCommands& commands) {
    const auto* command = commands.as_wordchain_or_null();
    return command == nullptr ? nullptr : &command->move;
  }
  static const Update* UpdateOf(const moonbase::games::GameEvents& events) {
    const auto* event = events.as_wordchain_or_null();
    return event == nullptr ? nullptr : &event->update;
  }
  static moonbase::games::GameEvents Wrap(Update update);

  static std::string Phase(const wordchain_race::Table& table);
  static bool Practice(const wordchain_race::Table& table) { return table.practice(); }
  // Everyone climbs at once: nobody is ever to move.
  static std::string Turn(const wordchain_race::Table&) { return ""; }
  static std::size_t GamesFinished(const wordchain_race::Table& table) {
    return table.scoreSheet().size();
  }
  static StatsDeltas ClosedDeltas(const wordchain_race::Table& table,
                                  const std::vector<std::string>& roster);
  static std::optional<StatsDeltas> DealEndDeltas(const wordchain_race::Table& before,
                                                  const wordchain_race::Table& after);
  static bool DealStarted(const wordchain_race::Table& before,
                          const wordchain_race::Table& after) {
    return !after.practice() && before.race().isOver() && !after.race().isOver();
  }
  // As go's: a race's end, the table racing on or closed by the leave that
  // ended it. Practice ends nothing on record.
  static std::optional<GameEnd> Ended(const wordchain_race::Table& table) {
    if (table.practice() || !table.race().isOver() ||
        (table.isOver() && !table.endedByClose())) {
      return std::nullopt;
    }
    return table.race().ending() == wordchain_race::Ending::kLeave ? GameEnd::kAbandoned
                                                                   : GameEnd::kCompleted;
  }
  static absl::StatusOr<wordchain_race::Table> WithoutSeat(const wordchain_race::Table& table,
                                                           int seat, int64_t) {
    return table.removePlayer(seat);
  }

  // A seat sees its own ladder, and everyone else's only as a count until
  // the race is over: a ladder in view would show the way.
  static View ViewOf(const TableContext& context, const wordchain_race::Table* table,
                     const std::string& viewer);
  static moonbase::games::GameEvents StateEvent(View view);
  static moonbase::games::GameEvents JoinedEvent(View view);
  static std::vector<moonbase::games::GameEvents> GamesEnded(const wordchain_race::Table& table,
                                                             std::size_t told);
  static std::optional<moonbase::games::GameEvents> ClosedEvent(const wordchain_race::Table&,
                                                                const std::vector<std::string>&) {
    return std::nullopt;
  }

  // Plays and take-backs, against the clock as each is applied.
  static Action<wordchain_race::Table, Start> Decode(const Move& move,
                                                     const std::string& player_id);
  // The puzzle is seeded by the hub's shuffle, never by the starter, who
  // could otherwise name one they know.
  static absl::StatusOr<wordchain_race::Table> Open(const OpenContext& context,
                                                    const Start& start,
                                                    std::deque<cards::Card> deck);
  /// A seek's table starts once its second seat fills, on its letters,
  /// after a 3 s countdown.
  static std::optional<StartGame<Start>> FilledStart(const std::optional<HostedTerms>& terms,
                                                     const std::optional<EventTag>&);
  /// A seek's table is heads-up.
  static std::size_t SeatsFor(const std::optional<HostedTerms>& terms) {
    return terms.has_value() ? 2 : kMaxSeats;
  }
  // {"letters", "deadlineMs", "expired"}.
  static nlohmann::json TermsJson(const WordchainTerms& terms);
  static std::optional<WordchainTerms> TermsFromJson(const nlohmann::json& stored);
  // "4 letters", or "any letters".
  static std::string TermsLine(const WordchainTerms& terms);
  static std::string Serialize(const wordchain_race::Table& engine);
  static absl::StatusOr<wordchain_race::Table> Deserialize(const std::string& stored);
  static bool IsBot(std::string_view) { return false; }

  static absl::StatusOr<wordchain_race::Table> Next(const wordchain_race::Table& table,
                                                    const Start& start,
                                                    const MoveContext& context);
  static bool CurrentGameOver(const wordchain_race::Table& table) {
    return table.race().isOver();
  }

  static bool FlagDue(const wordchain_race::Table& table, int64_t now_ms) {
    return !table.isOver() && table.race().timeUp(now_ms);
  }
  static absl::StatusOr<wordchain_race::Table> Flag(const wordchain_race::Table& table,
                                                    int64_t now_ms) {
    return table.inRace(
        [now_ms](const wordchain_race::Race& race) { return race.expire(now_ms); });
  }
};

/// How long a quick race's seek waits in the pool, and how long its race
/// counts down once matched.
inline constexpr int64_t kQuickSeekMs = 90'000;
inline constexpr int64_t kQuickCountdownMs = 3'000;

/// A shuffled deck as a puzzle seed: the same order, the same seed.
uint64_t WordchainSeed(const std::deque<cards::Card>& deck);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_WORDCHAIN_ADAPTER_H
