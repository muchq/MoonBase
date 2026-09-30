#include "domains/games/libs/chess_play/game_state_serde.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace chess_play {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 1;

// postgres jsonb refuses a NUL byte; it gets invalid UTF-8's treatment.
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

absl::Status Missing(const char* key, const char* kind) {
  return absl::InvalidArgumentError(absl::StrCat("expected ", kind, " field '", key, "'"));
}

absl::StatusOr<int64_t> ReadInt(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_number_integer()) return Missing(key, "integer");
  return object[key].get<int64_t>();
}

absl::StatusOr<std::string> ReadString(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_string()) return Missing(key, "string");
  return object[key].get<std::string>();
}

absl::StatusOr<std::vector<std::string>> ReadStrings(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_array()) return Missing(key, "array");
  std::vector<std::string> strings;
  for (const json& item : object[key]) {
    if (!item.is_string()) {
      return absl::InvalidArgumentError(absl::StrCat("expected strings in '", key, "'"));
    }
    strings.push_back(item.get<std::string>());
  }
  return strings;
}

absl::StatusOr<json> ReadObject(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_object()) return Missing(key, "object");
  return object[key];
}

absl::StatusOr<std::optional<Result>> ReadResult(const json& payload) {
  if (!payload.contains("result")) return std::optional<Result>{};
  auto object = ReadObject(payload, "result");
  if (!object.ok()) return object.status();
  auto ending_name = ReadString(*object, "ending");
  if (!ending_name.ok()) return ending_name.status();
  const std::optional<Ending> ending = ParseEnding(*ending_name);
  if (!ending.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("unknown ending: ", *ending_name));
  }
  Result result{std::nullopt, *ending};
  if (object->contains("winner")) {
    auto winner = ReadString(*object, "winner");
    if (!winner.ok()) return winner.status();
    if (*winner == ColorName(Color::kWhite)) {
      result.winner = Color::kWhite;
    } else if (*winner == ColorName(Color::kBlack)) {
      result.winner = Color::kBlack;
    } else {
      return absl::InvalidArgumentError(absl::StrCat("unknown winner: ", *winner));
    }
  }
  return std::optional<Result>(result);
}

}  // namespace

std::string serializeGameState(const GameState& state) {
  json players = json::array();
  for (const std::string& id : state.players()) players.push_back(Sanitized(id));
  json payload{
      {"v", kSchemaVersion},
      {"players", std::move(players)},
      {"variant", state.variant()},
      {"whiteSeat", state.whiteSeat()},
      {"startFen", state.startFen()},
      {"moves", state.moves()},
      {"timeControl",
       {{"initialMs", state.timeControl().initial_ms},
        {"incrementMs", state.timeControl().increment_ms}}},
      {"clock",
       {{"whiteMs", state.clock().remaining_ms[0]},
        {"blackMs", state.clock().remaining_ms[1]},
        {"turnStartedMs", state.clock().turn_started_ms}}},
  };
  if (const auto& result = state.result(); result.has_value()) {
    json object{{"ending", EndingName(result->ending)}};
    if (result->winner.has_value()) object["winner"] = ColorName(*result->winner);
    payload["result"] = std::move(object);
  }
  return payload.dump(/*indent=*/-1, /*indent_char=*/' ', /*ensure_ascii=*/false,
                      json::error_handler_t::replace);
}

absl::StatusOr<GameState> deserializeGameState(const std::string& serialized) {
  const json payload = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (payload.is_discarded() || !payload.is_object()) {
    return absl::InvalidArgumentError("not a stored chess game");
  }
  auto version = ReadInt(payload, "v");
  if (!version.ok()) return version.status();
  if (*version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown schema version: ", *version));
  }
  auto players = ReadStrings(payload, "players");
  if (!players.ok()) return players.status();
  auto variant = ReadString(payload, "variant");
  if (!variant.ok()) return variant.status();
  auto white_seat = ReadInt(payload, "whiteSeat");
  if (!white_seat.ok()) return white_seat.status();
  auto start_fen = ReadString(payload, "startFen");
  if (!start_fen.ok()) return start_fen.status();
  auto moves = ReadStrings(payload, "moves");
  if (!moves.ok()) return moves.status();

  auto tc_object = ReadObject(payload, "timeControl");
  if (!tc_object.ok()) return tc_object.status();
  auto initial = ReadInt(*tc_object, "initialMs");
  if (!initial.ok()) return initial.status();
  auto increment = ReadInt(*tc_object, "incrementMs");
  if (!increment.ok()) return increment.status();

  auto clock_object = ReadObject(payload, "clock");
  if (!clock_object.ok()) return clock_object.status();
  auto white_ms = ReadInt(*clock_object, "whiteMs");
  if (!white_ms.ok()) return white_ms.status();
  auto black_ms = ReadInt(*clock_object, "blackMs");
  if (!black_ms.ok()) return black_ms.status();
  auto turn_started = ReadInt(*clock_object, "turnStartedMs");
  if (!turn_started.ok()) return turn_started.status();

  auto result = ReadResult(payload);
  if (!result.ok()) return result.status();

  if (*white_seat != 0 && *white_seat != 1) {
    return absl::InvalidArgumentError("white seat is 0 or 1");
  }
  Clock clock;
  clock.remaining_ms = {*white_ms, *black_ms};
  clock.turn_started_ms = *turn_started;
  return GameState::restore(*std::move(players), *std::move(variant), *std::move(start_fen),
                            static_cast<int>(*white_seat), *std::move(moves),
                            TimeControl{*initial, *increment}, clock, *std::move(result));
}

}  // namespace chess_play
