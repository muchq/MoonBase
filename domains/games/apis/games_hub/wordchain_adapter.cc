#include "domains/games/apis/games_hub/wordchain_adapter.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "domains/games/libs/wordchain_race/lexicon.h"
#include "domains/games/libs/wordchain_race/table_serde.h"

namespace games_hub {
namespace {

using wordchain_race::Ending;
using wordchain_race::Race;
using wordchain_race::RaceScore;
using wordchain_race::Table;

std::string EndingWord(Ending ending) {
  switch (ending) {
    case Ending::kFinished:
      return "finished";
    case Ending::kTimeout:
      return "timeout";
    case Ending::kLeave:
      return "leave";
  }
  return "finished";
}

// Every runner raced, seated or not, and the winner won.
StatsDeltas RaceDeltas(const Race& race) {
  const std::optional<std::string> winner = race.winner();
  StatsDeltas deltas;
  for (const wordchain_race::Runner& runner : race.runners()) {
    HubStore::StatsDelta delta;
    delta.player_id = runner.player_id;
    delta.played = 1;
    delta.won = winner == runner.player_id ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

moonbase::games::WordchainResult ResultOf(const RaceScore& score,
                                          std::vector<std::string> standings) {
  moonbase::games::WordchainResult result;
  result.ending = EndingWord(score.ending);
  result.winner = score.winner;
  result.standings = std::move(standings);
  return result;
}

absl::StatusOr<wordchain_race::Puzzle> PickFor(const Adapter<Table>::Start& start, uint64_t seed) {
  const wordchain_race::Lexicon& lexicon = wordchain_race::EnableLexicon();
  auto puzzle = wordchain_race::PickPuzzle(lexicon.words, lexicon.endpoints, start.letters, seed);
  if (!puzzle.ok()) return puzzle.status();
  puzzle->lexicon = std::string(lexicon.name);
  return puzzle;
}

}  // namespace

uint64_t WordchainSeed(const std::deque<cards::Card>& deck) {
  // FNV-1a over each card's rank and suit.
  uint64_t seed = 0xcbf29ce484222325ULL;
  for (const cards::Card& card : deck) {
    for (const int part : {static_cast<int>(card.getRank()), static_cast<int>(card.getSuit())}) {
      seed ^= static_cast<uint64_t>(part);
      seed *= 0x100000001b3ULL;
    }
  }
  return seed;
}

std::string Adapter<Table>::Phase(const Table& table) {
  if (table.isOver()) return "closed";
  return table.race().isOver() ? "over" : "racing";
}

StatsDeltas Adapter<Table>::ClosedDeltas(const Table& table, const std::vector<std::string>&) {
  return table.endedByClose() ? RaceDeltas(table.race()) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<Table>::DealEndDeltas(const Table& before, const Table& after) {
  if (before.race().isOver() || !after.race().isOver() || after.isOver()) return std::nullopt;
  return RaceDeltas(after.race());
}

moonbase::games::GameEvents Adapter<Table>::Wrap(Update update) {
  moonbase::games::WordchainEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromWordchain(std::move(event));
}

moonbase::games::WordchainView Adapter<Table>::ViewOf(const TableContext& context,
                                                      const Table* table,
                                                      const std::string& viewer) {
  moonbase::games::WordchainView view;
  view.gameId = context.game_id;
  if (table == nullptr) {
    view.phase = "waiting";
    view.racesPlayed = 0;
    for (const std::string& id : context.roster) {
      moonbase::games::WordchainRunner runner;
      runner.playerId = id;
      runner.words = 0;
      runner.left = false;
      runner.wins = 0;
      view.runners.push_back(std::move(runner));
    }
    return view;
  }
  const Race& race = table->race();
  const wordchain_race::Puzzle& puzzle = race.puzzle();
  view.phase = Phase(*table);
  view.start = puzzle.start;
  view.target = puzzle.target;
  view.par = puzzle.optimal;
  view.startedAtMs = race.startedMs();
  view.deadlineMs = race.deadlineMs();
  view.lexicon = puzzle.lexicon;
  view.racesPlayed = static_cast<int>(table->scoreSheet().size());
  for (const wordchain_race::Runner& runner : race.runners()) {
    moonbase::games::WordchainRunner wire;
    wire.playerId = runner.player_id;
    wire.words = static_cast<int>(runner.ladder.size());
    wire.finishedAtMs = runner.finished_ms;
    wire.left = runner.left;
    if (runner.player_id == viewer || race.isOver()) wire.ladder = runner.ladder;
    wire.wins = static_cast<int>(
        std::count_if(table->scoreSheet().begin(), table->scoreSheet().end(),
                      [&](const RaceScore& score) { return score.winner == runner.player_id; }));
    view.runners.push_back(std::move(wire));
  }
  if (race.isOver()) view.result = ResultOf(wordchain_race::ScoreOf(race), race.standings());
  return view;
}

moonbase::games::GameEvents Adapter<Table>::StateEvent(View view) {
  moonbase::games::WordchainGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<Table>::JoinedEvent(View view) {
  moonbase::games::WordchainGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// Read off the score sheet, so a race that ended before the next began is
// owed all the same; only the last race still holds its standings.
std::vector<moonbase::games::GameEvents> Adapter<Table>::GamesEnded(const Table& table,
                                                                    std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  const std::vector<RaceScore>& sheet = table.scoreSheet();
  for (std::size_t i = told; i < sheet.size(); ++i) {
    const bool last = i + 1 == sheet.size() && table.race().isOver();
    moonbase::games::WordchainGameEnded ended;
    ended.result = ResultOf(sheet[i], last ? table.race().standings() : std::vector<std::string>{});
    events.push_back(Wrap(Update::FromGameended(std::move(ended))));
  }
  return events;
}

Action<Table, Adapter<Table>::Start> Adapter<Table>::Decode(const Move& move, const std::string&) {
  using RaceTransition = Transition<Table>;
  if (const auto* start = move.as_startGame_or_null()) {
    Start named;
    if (start->letters.has_value()) named.letters = *start->letters;
    if (start->seconds.has_value()) named.duration_ms = int64_t{*start->seconds} * 1000;
    if (named.letters < wordchain_race::kMinLetters ||
        named.letters > wordchain_race::kMaxLetters) {
      return Refusal{RejectKind::kInvalid, absl::StrCat("a race is ", wordchain_race::kMinLetters,
                                                        " to ", wordchain_race::kMaxLetters,
                                                        " letters")};
    }
    if (named.duration_ms < 60'000 || named.duration_ms > 600'000) {
      return Refusal{RejectKind::kInvalid, "a race runs 60 to 600 seconds"};
    }
    return StartGame<Start>{named, /*next_game=*/true};
  }
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  if (const auto* play = move.as_play_or_null()) {
    return RaceTransition{[word = play->word](const Table& table, int seat,
                                              const MoveContext& context) {
      return table.inRace([&](const Race& race) {
        return race.play(seat, word, wordchain_race::EnableLexicon().words, context.now_ms);
      });
    }};
  }
  if (move.as_retract_or_null() != nullptr) {
    return RaceTransition{[](const Table& table, int seat, const MoveContext& context) {
      return table.inRace([&](const Race& race) { return race.retract(seat, context.now_ms); });
    }};
  }
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

absl::StatusOr<Table> Adapter<Table>::Open(const OpenContext& context, const Start& start,
                                           std::deque<cards::Card> deck) {
  auto puzzle = PickFor(start, WordchainSeed(deck));
  if (!puzzle.ok()) return puzzle.status();
  return Table::open(context.roster, *std::move(puzzle), context.now_ms, start.duration_ms);
}

absl::StatusOr<Table> Adapter<Table>::Next(const Table& table, const Start& start,
                                           const MoveContext& context) {
  auto puzzle = PickFor(start, WordchainSeed(context.deck()));
  if (!puzzle.ok()) return puzzle.status();
  return table.next(*std::move(puzzle), context.now_ms, start.duration_ms);
}

std::string Adapter<Table>::Serialize(const Table& engine) {
  return wordchain_race::serializeTable(engine);
}

absl::StatusOr<Table> Adapter<Table>::Deserialize(const std::string& stored) {
  return wordchain_race::deserializeTable(stored);
}

}  // namespace games_hub
