#include "domains/games/libs/chess_play/table_serde.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/chess_play/game_state_serde.h"

namespace chess_play {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 2;
// A row from before tables: the game alone.
constexpr int kGameRowVersion = 1;

// As the game's own ids are stored: postgres jsonb refuses a NUL byte.
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

absl::StatusOr<std::vector<GameScore>> ReadSheet(const json& payload) {
  if (!payload.contains("scoreSheet") || !payload["scoreSheet"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'scoreSheet'");
  }
  std::vector<GameScore> sheet;
  for (const json& line : payload["scoreSheet"]) {
    if (!line.is_object() || !line.contains("ending") || !line["ending"].is_string()) {
      return absl::InvalidArgumentError("a score line needs its ending");
    }
    const std::optional<Ending> ending = ParseEnding(line["ending"].get<std::string>());
    if (!ending.has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown ending: ", line["ending"].get<std::string>()));
    }
    GameScore score;
    score.ending = *ending;
    if (line.contains("winner")) {
      if (!line["winner"].is_string()) return absl::InvalidArgumentError("a winner is a name");
      score.winner = line["winner"].get<std::string>();
    }
    sheet.push_back(std::move(score));
  }
  return sheet;
}

absl::StatusOr<bool> ReadBool(const json& payload, const char* key) {
  if (!payload.contains(key) || !payload[key].is_boolean()) {
    return absl::InvalidArgumentError(absl::StrCat("expected boolean field '", key, "'"));
  }
  return payload[key].get<bool>();
}

}  // namespace

std::string serializeTable(const Table& table) {
  json sheet = json::array();
  for (const GameScore& score : table.scoreSheet()) {
    json line{{"ending", EndingName(score.ending)}};
    if (score.winner.has_value()) line["winner"] = Sanitized(*score.winner);
    sheet.push_back(std::move(line));
  }
  const json payload{
      {"v", kSchemaVersion},
      {"closed", table.isOver()},
      {"endedByClose", table.endedByClose()},
      {"game", json::parse(serializeGameState(table.game()))},
      {"scoreSheet", std::move(sheet)},
  };
  return payload.dump(/*indent=*/-1, /*indent_char=*/' ', /*ensure_ascii=*/false,
                      json::error_handler_t::replace);
}

absl::StatusOr<Table> deserializeTable(const std::string& serialized) {
  const json payload = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (payload.is_discarded() || !payload.is_object() || !payload.contains("v") ||
      !payload["v"].is_number_integer()) {
    return absl::InvalidArgumentError("not a stored chess table");
  }
  const int64_t version = payload["v"].get<int64_t>();
  if (version == kGameRowVersion) {
    auto game = deserializeGameState(serialized);
    if (!game.ok()) return game.status();
    const bool over = game->isOver();
    std::vector<GameScore> sheet;
    if (over) sheet.push_back(ScoreOf(*game));
    return Table::restore(*std::move(game), std::move(sheet), over, over);
  }
  if (version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown schema version: ", version));
  }
  auto closed = ReadBool(payload, "closed");
  if (!closed.ok()) return closed.status();
  auto ended_by_close = ReadBool(payload, "endedByClose");
  if (!ended_by_close.ok()) return ended_by_close.status();
  if (!payload.contains("game") || !payload["game"].is_object()) {
    return absl::InvalidArgumentError("expected object field 'game'");
  }
  auto game = deserializeGameState(payload["game"].dump());
  if (!game.ok()) return game.status();
  auto sheet = ReadSheet(payload);
  if (!sheet.ok()) return sheet.status();
  return Table::restore(*std::move(game), *std::move(sheet), *closed, *ended_by_close);
}

}  // namespace chess_play
