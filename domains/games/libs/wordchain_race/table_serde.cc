#include "domains/games/libs/wordchain_race/table_serde.h"

#include <nlohmann/json.hpp>

#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace wordchain_race {
namespace {

using nlohmann::json;

constexpr int kVersion = 1;

std::string_view EndingName(Ending ending) {
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

absl::StatusOr<Ending> EndingNamed(std::string_view name) {
  for (Ending ending : {Ending::kFinished, Ending::kTimeout, Ending::kLeave}) {
    if (EndingName(ending) == name) return ending;
  }
  return absl::InvalidArgumentError(absl::StrCat("no ending ", name));
}

absl::StatusOr<const json*> Field(const json& object, std::string_view key) {
  const auto found = object.find(key);
  if (found == object.end()) return absl::InvalidArgumentError(absl::StrCat(key, " is missing"));
  return &*found;
}

absl::StatusOr<int64_t> Integer(const json& object, std::string_view key) {
  auto field = Field(object, key);
  if (!field.ok()) return field.status();
  if (!(*field)->is_number_integer()) {
    return absl::InvalidArgumentError(absl::StrCat(key, " is an integer"));
  }
  return (*field)->get<int64_t>();
}

absl::StatusOr<std::string> String(const json& object, std::string_view key) {
  auto field = Field(object, key);
  if (!field.ok()) return field.status();
  if (!(*field)->is_string()) return absl::InvalidArgumentError(absl::StrCat(key, " is text"));
  return (*field)->get<std::string>();
}

absl::StatusOr<bool> Boolean(const json& object, std::string_view key) {
  auto field = Field(object, key);
  if (!field.ok()) return field.status();
  if (!(*field)->is_boolean()) return absl::InvalidArgumentError(absl::StrCat(key, " is a flag"));
  return (*field)->get<bool>();
}

absl::StatusOr<const json*> Array(const json& object, std::string_view key) {
  auto field = Field(object, key);
  if (!field.ok()) return field.status();
  if (!(*field)->is_array()) return absl::InvalidArgumentError(absl::StrCat(key, " is a list"));
  return *field;
}

absl::StatusOr<Runner> ReadRunner(const json& r) {
  if (!r.is_object()) return absl::InvalidArgumentError("a runner is an object");
  auto id = String(r, "id");
  if (!id.ok()) return id.status();
  auto ladder_field = Array(r, "ladder");
  if (!ladder_field.ok()) return ladder_field.status();
  auto left = Boolean(r, "left");
  if (!left.ok()) return left.status();
  Runner runner{*std::move(id), {}, std::nullopt, *left};
  for (const json& word : **ladder_field) {
    if (!word.is_string()) return absl::InvalidArgumentError("a rung is a word");
    runner.ladder.push_back(word.get<std::string>());
  }
  if (r.contains("finishedMs")) {
    auto finished = Integer(r, "finishedMs");
    if (!finished.ok()) return finished.status();
    runner.finished_ms = *finished;
  }
  return runner;
}

}  // namespace

std::string serializeTable(const Table& table) {
  const Race& race = table.race();
  json runners = json::array();
  for (const Runner& runner : race.runners()) {
    json r = {{"id", runner.player_id}, {"ladder", runner.ladder}, {"left", runner.left}};
    if (runner.finished_ms.has_value()) r["finishedMs"] = *runner.finished_ms;
    runners.push_back(std::move(r));
  }
  json sheet = json::array();
  for (const RaceScore& score : table.scoreSheet()) {
    json line = {{"ending", EndingName(score.ending)}};
    if (score.winner.has_value()) line["winner"] = *score.winner;
    sheet.push_back(std::move(line));
  }
  const Puzzle& puzzle = race.puzzle();
  json row = {
      {"v", kVersion},
      {"puzzle",
       {{"start", puzzle.start},
        {"target", puzzle.target},
        {"optimal", puzzle.optimal},
        {"lexicon", puzzle.lexicon}}},
      {"runners", std::move(runners)},
      {"startedMs", race.startedMs()},
      {"deadlineMs", race.deadlineMs()},
      {"scoreSheet", std::move(sheet)},
      {"closed", table.isOver()},
      {"endedByClose", table.endedByClose()},
  };
  if (race.ending().has_value()) row["ending"] = EndingName(*race.ending());
  return row.dump();
}

absl::StatusOr<Table> deserializeTable(const std::string& serialized) {
  const json j = json::parse(serialized, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) return absl::InvalidArgumentError("not a race row");
  auto version = Integer(j, "v");
  if (!version.ok()) return version.status();
  if (*version != kVersion) {
    return absl::InvalidArgumentError(absl::StrCat("race row version ", *version));
  }

  auto puzzle_field = Field(j, "puzzle");
  if (!puzzle_field.ok()) return puzzle_field.status();
  const json& p = **puzzle_field;
  if (!p.is_object()) return absl::InvalidArgumentError("puzzle is an object");
  auto start = String(p, "start");
  auto target = String(p, "target");
  auto optimal = Integer(p, "optimal");
  auto lexicon = String(p, "lexicon");
  if (!lexicon.ok()) return lexicon.status();
  if (!start.ok()) return start.status();
  if (!target.ok()) return target.status();
  if (!optimal.ok()) return optimal.status();
  if (*optimal < 0 || *optimal > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError("optimal is out of range");
  }

  auto runners_field = Array(j, "runners");
  if (!runners_field.ok()) return runners_field.status();
  std::vector<Runner> runners;
  for (const json& r : **runners_field) {
    auto runner = ReadRunner(r);
    if (!runner.ok()) return runner.status();
    runners.push_back(*std::move(runner));
  }

  auto sheet_field = Array(j, "scoreSheet");
  if (!sheet_field.ok()) return sheet_field.status();
  std::vector<RaceScore> sheet;
  for (const json& line : **sheet_field) {
    if (!line.is_object()) return absl::InvalidArgumentError("a score is an object");
    RaceScore score;
    auto ending_name = String(line, "ending");
    if (!ending_name.ok()) return ending_name.status();
    auto ending = EndingNamed(*ending_name);
    if (!ending.ok()) return ending.status();
    score.ending = *ending;
    if (line.contains("winner")) {
      auto winner = String(line, "winner");
      if (!winner.ok()) return winner.status();
      score.winner = *std::move(winner);
    }
    sheet.push_back(std::move(score));
  }

  auto started = Integer(j, "startedMs");
  auto deadline = Integer(j, "deadlineMs");
  auto closed = Boolean(j, "closed");
  auto ended_by_close = Boolean(j, "endedByClose");
  for (const absl::Status& s :
       {started.status(), deadline.status(), closed.status(), ended_by_close.status()}) {
    if (!s.ok()) return s;
  }
  std::optional<Ending> race_ending;
  if (j.contains("ending")) {
    auto name = String(j, "ending");
    if (!name.ok()) return name.status();
    auto ending = EndingNamed(*name);
    if (!ending.ok()) return ending.status();
    race_ending = *ending;
  }
  auto race = Race::restore(Puzzle{*std::move(start), *std::move(target), static_cast<int>(*optimal),
                                   *std::move(lexicon)},
                            std::move(runners), *started, *deadline, race_ending);
  if (!race.ok()) return race.status();
  return Table::restore(*std::move(race), std::move(sheet), *closed, *ended_by_close);
}

}  // namespace wordchain_race
