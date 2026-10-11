#include "domains/games/libs/pong/table_serde.h"

#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace pong {
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

absl::StatusOr<const json*> Field(const json& payload, const char* name) {
  if (!payload.contains(name)) return absl::InvalidArgumentError(absl::StrCat("missing ", name));
  return &payload[name];
}

absl::StatusOr<bool> Flag(const json& payload, const char* name) {
  const auto field = Field(payload, name);
  if (!field.ok()) return field.status();
  if (!(*field)->is_boolean()) return absl::InvalidArgumentError(absl::StrCat(name, " is a bool"));
  return (*field)->get<bool>();
}

absl::StatusOr<std::vector<GameScore>> Sheet(const json& payload) {
  const auto field = Field(payload, "scoreSheet");
  if (!field.ok()) return field.status();
  if (!(*field)->is_array()) return absl::InvalidArgumentError("scoreSheet is an array");
  std::vector<GameScore> sheet;
  for (const json& line : **field) {
    if (!line.is_object() || !line.contains("ending") || !line["ending"].is_string()) {
      return absl::InvalidArgumentError("a score line needs its ending");
    }
    const std::optional<Ending> ending = ParseEnding(line["ending"].get<std::string>());
    if (!ending.has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown ending: ", line["ending"].get<std::string>()));
    }
    GameScore score{std::nullopt, *ending};
    if (line.contains("winner")) {
      if (!line["winner"].is_string()) return absl::InvalidArgumentError("a winner is a name");
      score.winner = line["winner"].get<std::string>();
    }
    sheet.push_back(std::move(score));
  }
  return sheet;
}

}  // namespace

std::string serializeTable(const Table& table) {
  json sheet = json::array();
  for (const GameScore& line : table.scoreSheet()) {
    json entry{{"ending", EndingName(line.ending)}};
    if (line.winner.has_value()) entry["winner"] = Sanitized(*line.winner);
    sheet.push_back(std::move(entry));
  }
  json players = json::array();
  for (const std::string& id : table.players()) players.push_back(Sanitized(id));
  const json row{
      {"v", kSchemaVersion},           {"variant", VariantName(table.variant())},
      {"players", std::move(players)}, {"lives", json::array({table.lives()[0], table.lives()[1]})},
      {"points", table.points()},      {"inPlay", table.inPlay()},
      {"closed", table.isOver()},      {"endedByClose", table.endedByClose()},
      {"scoreSheet", std::move(sheet)}};
  return row.dump();
}

absl::StatusOr<Table> deserializeTable(const std::string& serialized) {
  const json payload = json::parse(serialized, nullptr, /*allow_exceptions=*/false);
  if (payload.is_discarded() || !payload.is_object()) {
    return absl::InvalidArgumentError("a pong row is a JSON object");
  }
  const auto version = Field(payload, "v");
  if (!version.ok()) return version.status();
  if (**version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown pong schema: ", (*version)->dump()));
  }
  const auto variant_field = Field(payload, "variant");
  if (!variant_field.ok()) return variant_field.status();
  const std::optional<Variant> variant = (*variant_field)->is_string()
                                             ? ParseVariant((*variant_field)->get<std::string>())
                                             : std::nullopt;
  if (!variant.has_value()) return absl::InvalidArgumentError("unknown pong variant");

  const auto players_field = Field(payload, "players");
  if (!players_field.ok()) return players_field.status();
  std::vector<std::string> players;
  if (!(*players_field)->is_array()) return absl::InvalidArgumentError("players is an array");
  for (const json& id : **players_field) {
    if (!id.is_string()) return absl::InvalidArgumentError("a player is a name");
    players.push_back(id.get<std::string>());
  }

  const auto lives_field = Field(payload, "lives");
  if (!lives_field.ok()) return lives_field.status();
  const json& lives_json = **lives_field;
  if (!lives_json.is_array() || lives_json.size() != kSeats || !lives_json[0].is_number_integer() ||
      !lives_json[1].is_number_integer()) {
    return absl::InvalidArgumentError("lives are two counts");
  }
  const std::array<int, kSeats> lives{lives_json[0].get<int>(), lives_json[1].get<int>()};

  const auto points_field = Field(payload, "points");
  if (!points_field.ok()) return points_field.status();
  if (!(*points_field)->is_number_integer()) return absl::InvalidArgumentError("points is a count");

  const auto in_play = Flag(payload, "inPlay");
  if (!in_play.ok()) return in_play.status();
  const auto closed = Flag(payload, "closed");
  if (!closed.ok()) return closed.status();
  const auto ended_by_close = Flag(payload, "endedByClose");
  if (!ended_by_close.ok()) return ended_by_close.status();
  auto sheet = Sheet(payload);
  if (!sheet.ok()) return sheet.status();
  return Table::restore(std::move(players), *variant, lives, (*points_field)->get<int>(),
                        *std::move(sheet), *in_play, *closed, *ended_by_close);
}

}  // namespace pong
