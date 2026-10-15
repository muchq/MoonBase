#include "domains/games/libs/go/table_serde.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace go {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 1;

// As chess's rows are stored: postgres jsonb refuses a NUL byte.
std::string Sanitized(const std::string& text) {
  std::string safe;
  for (const char c : text) {
    if (c == '\0') {
      safe += "\xEF\xBF\xBD";
    } else {
      safe += c;
    }
  }
  return safe;
}

std::optional<Color> ParseColor(const json& name) {
  if (name == ColorName(Color::kBlack)) return Color::kBlack;
  if (name == ColorName(Color::kWhite)) return Color::kWhite;
  return std::nullopt;
}

absl::StatusOr<const json*> Field(const json& payload, const char* name) {
  if (!payload.contains(name)) return absl::InvalidArgumentError(absl::StrCat("missing ", name));
  return &payload[name];
}

absl::StatusOr<int64_t> Integer(const json& payload, const char* name) {
  const auto field = Field(payload, name);
  if (!field.ok()) return field.status();
  if (!(*field)->is_number_integer()) {
    return absl::InvalidArgumentError(absl::StrCat(name, " is an integer"));
  }
  return (*field)->get<int64_t>();
}

absl::StatusOr<bool> Flag(const json& payload, const char* name) {
  const auto field = Field(payload, name);
  if (!field.ok()) return field.status();
  if (!(*field)->is_boolean()) return absl::InvalidArgumentError(absl::StrCat(name, " is a bool"));
  return (*field)->get<bool>();
}

absl::StatusOr<Ending> ReadEnding(const json& entry) {
  if (!entry.is_object() || !entry.contains("ending") || !entry["ending"].is_string()) {
    return absl::InvalidArgumentError("an ending is named");
  }
  const std::optional<Ending> ending = ParseEnding(entry["ending"].get<std::string>());
  if (!ending.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown ending: ", entry["ending"].get<std::string>()));
  }
  return *ending;
}

absl::StatusOr<std::optional<Color>> ReadColor(const json& entry, const char* name) {
  if (!entry.contains(name)) return std::optional<Color>();
  const std::optional<Color> color = ParseColor(entry[name]);
  if (!color.has_value()) return absl::InvalidArgumentError("a colour is black or white");
  return color;
}

absl::StatusOr<std::vector<GameScore>> ReadSheet(const json& payload) {
  const auto field = Field(payload, "scoreSheet");
  if (!field.ok()) return field.status();
  if (!(*field)->is_array()) return absl::InvalidArgumentError("scoreSheet is an array");
  std::vector<GameScore> sheet;
  for (const json& line : **field) {
    const auto ending = ReadEnding(line);
    if (!ending.ok()) return ending.status();
    GameScore score;
    score.ending = *ending;
    if (line.contains("winner")) {
      if (!line["winner"].is_string()) return absl::InvalidArgumentError("a winner is a name");
      score.winner = line["winner"].get<std::string>();
    }
    auto color = ReadColor(line, "winnerColor");
    if (!color.ok()) return color.status();
    score.winner_color = *color;
    sheet.push_back(std::move(score));
  }
  return sheet;
}

}  // namespace

std::string serializeTable(const Table& table) {
  const GameState& game = table.game();
  json players = json::array();
  for (const std::string& id : game.players()) players.push_back(Sanitized(id));
  json sheet = json::array();
  for (const GameScore& line : table.scoreSheet()) {
    json entry{{"ending", EndingName(line.ending)}};
    if (line.winner.has_value()) entry["winner"] = Sanitized(*line.winner);
    if (line.winner_color.has_value()) entry["winnerColor"] = ColorName(*line.winner_color);
    sheet.push_back(std::move(entry));
  }
  json row{
      {"v", kSchemaVersion},
      {"players", std::move(players)},
      {"size", game.size()},
      {"komi", game.komi()},
      {"blackSeat", game.blackSeat()},
      {"moves", game.moves()},
      {"initialMs", game.timeControl().initial_ms},
      {"incrementMs", game.timeControl().increment_ms},
      {"remainingMs", json::array({game.clock().remaining_ms[0], game.clock().remaining_ms[1]})},
      {"turnStartedMs", game.clock().turn_started_ms},
      {"closed", table.isOver()},
      {"endedByClose", table.endedByClose()},
      {"scoreSheet", std::move(sheet)}};
  if (game.result().has_value()) {
    json result{{"ending", EndingName(game.result()->ending)}};
    if (game.result()->winner.has_value()) result["winner"] = ColorName(*game.result()->winner);
    row["result"] = std::move(result);
  }
  return row.dump();
}

absl::StatusOr<Table> deserializeTable(const std::string& serialized) {
  const json payload = json::parse(serialized, nullptr, /*allow_exceptions=*/false);
  if (payload.is_discarded() || !payload.is_object()) {
    return absl::InvalidArgumentError("a go row is a JSON object");
  }
  const auto version = Integer(payload, "v");
  if (!version.ok()) return version.status();
  if (*version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown go schema: ", *version));
  }

  const auto players_field = Field(payload, "players");
  if (!players_field.ok()) return players_field.status();
  if (!(*players_field)->is_array()) return absl::InvalidArgumentError("players is an array");
  std::vector<std::string> players;
  for (const json& id : **players_field) {
    if (!id.is_string()) return absl::InvalidArgumentError("a player is a name");
    players.push_back(id.get<std::string>());
  }

  const auto size = Integer(payload, "size");
  if (!size.ok()) return size.status();
  const auto komi_field = Field(payload, "komi");
  if (!komi_field.ok()) return komi_field.status();
  if (!(*komi_field)->is_number()) return absl::InvalidArgumentError("komi is a number");
  const auto black_seat = Integer(payload, "blackSeat");
  if (!black_seat.ok()) return black_seat.status();

  const auto moves_field = Field(payload, "moves");
  if (!moves_field.ok()) return moves_field.status();
  if (!(*moves_field)->is_array()) return absl::InvalidArgumentError("moves is an array");
  std::vector<int> moves;
  for (const json& move : **moves_field) {
    if (!move.is_number_integer()) return absl::InvalidArgumentError("a move is a point");
    moves.push_back(move.get<int>());
  }

  const auto initial = Integer(payload, "initialMs");
  if (!initial.ok()) return initial.status();
  const auto increment = Integer(payload, "incrementMs");
  if (!increment.ok()) return increment.status();
  const auto remaining_field = Field(payload, "remainingMs");
  if (!remaining_field.ok()) return remaining_field.status();
  const json& remaining = **remaining_field;
  if (!remaining.is_array() || remaining.size() != 2 || !remaining[0].is_number_integer() ||
      !remaining[1].is_number_integer()) {
    return absl::InvalidArgumentError("remainingMs is two times");
  }
  const auto turn_started = Integer(payload, "turnStartedMs");
  if (!turn_started.ok()) return turn_started.status();
  const Clock clock{{remaining[0].get<int64_t>(), remaining[1].get<int64_t>()}, *turn_started};

  std::optional<Result> result;
  if (payload.contains("result")) {
    const auto ending = ReadEnding(payload["result"]);
    if (!ending.ok()) return ending.status();
    auto winner = ReadColor(payload["result"], "winner");
    if (!winner.ok()) return winner.status();
    result = Result{*winner, *ending};
  }

  auto game =
      GameState::restore(std::move(players), static_cast<int>(*size), (*komi_field)->get<double>(),
                         static_cast<int>(*black_seat), std::move(moves),
                         TimeControl{*initial, *increment}, clock, std::move(result));
  if (!game.ok()) return game.status();
  const auto closed = Flag(payload, "closed");
  if (!closed.ok()) return closed.status();
  const auto ended_by_close = Flag(payload, "endedByClose");
  if (!ended_by_close.ok()) return ended_by_close.status();
  auto sheet = ReadSheet(payload);
  if (!sheet.ok()) return sheet.status();
  return Table::restore(*std::move(game), *std::move(sheet), *closed, *ended_by_close);
}

}  // namespace go
