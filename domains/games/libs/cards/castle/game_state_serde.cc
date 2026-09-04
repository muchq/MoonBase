#include "domains/games/libs/cards/castle/game_state_serde.h"

#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/player.h"

namespace castle {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 1;

template <typename Cards>
json cardsToJson(const Cards& cards) {
  json codes = json::array();
  for (const Card& card : cards) codes.push_back(card.intValue());
  return codes;
}

// postgres jsonb rejects a NUL byte, so it gets the same treatment as
// invalid UTF-8: U+FFFD, and the write path survives any player id.
std::string sanitized(const std::string& text) {
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

const char* phaseName(Phase phase) {
  switch (phase) {
    case Phase::Setup:
      return "setup";
    case Phase::Playing:
      return "playing";
    case Phase::Over:
      return "over";
    case Phase::Abandoned:
      return "abandoned";
  }
  return "setup";
}

json playerToJson(const Player& player) {
  return json{
      {"id", sanitized(player.getId())},
      {"hand", cardsToJson(player.getHand())},
      {"faceUp", cardsToJson(player.getFaceUp())},
      {"faceDown", cardsToJson(player.getFaceDown())},
      {"ready", player.isReady()},
  };
}

// Field readers that turn shape errors into invalid-argument statuses;
// no get<>() runs before an is_*() check of the same value, and integers
// range-check as int64 before narrowing.

absl::StatusOr<int> readIntInRange(const json& object, const char* key, int64_t lo, int64_t hi) {
  if (!object.contains(key) || !object[key].is_number_integer()) {
    return absl::InvalidArgumentError(absl::StrCat("expected integer field '", key, "'"));
  }
  const int64_t value = object[key].get<int64_t>();
  if (value < lo || value > hi) {
    return absl::InvalidArgumentError(absl::StrCat("field '", key, "' out of range"));
  }
  return static_cast<int>(value);
}

absl::StatusOr<bool> readBool(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_boolean()) {
    return absl::InvalidArgumentError(absl::StrCat("expected boolean field '", key, "'"));
  }
  return object[key].get<bool>();
}

absl::StatusOr<std::string> readString(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_string()) {
    return absl::InvalidArgumentError(absl::StrCat("expected string field '", key, "'"));
  }
  return object[key].get<std::string>();
}

absl::StatusOr<std::vector<Card>> readCardCodes(const json& object, const char* key,
                                                std::size_t max_size) {
  if (!object.contains(key) || !object[key].is_array()) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  std::vector<Card> cards;
  for (const json& code : object[key]) {
    if (!code.is_number_integer() || code.get<int64_t>() < 0 || code.get<int64_t>() > 51) {
      return absl::InvalidArgumentError(absl::StrCat("card code out of range in '", key, "'"));
    }
    cards.emplace_back(static_cast<int>(code.get<int64_t>()));
  }
  if (cards.size() > max_size) {
    return absl::InvalidArgumentError(absl::StrCat("too many cards in '", key, "'"));
  }
  return cards;
}

absl::StatusOr<Phase> readPhase(const json& object) {
  auto name = readString(object, "phase");
  if (!name.ok()) return name.status();
  if (*name == "setup") return Phase::Setup;
  if (*name == "playing") return Phase::Playing;
  if (*name == "over") return Phase::Over;
  if (*name == "abandoned") return Phase::Abandoned;
  return absl::InvalidArgumentError("unknown phase");
}

absl::StatusOr<Player> readPlayer(const json& object) {
  if (!object.is_object()) return absl::InvalidArgumentError("expected player object");
  auto id = readString(object, "id");
  if (!id.ok()) return id.status();
  // A hand grows with every pick-up, so only the deck bounds it; the
  // table rows never exceed the deal.
  auto hand = readCardCodes(object, "hand", 52);
  if (!hand.ok()) return hand.status();
  auto face_up = readCardCodes(object, "faceUp", GameState::kHandSize);
  if (!face_up.ok()) return face_up.status();
  auto face_down = readCardCodes(object, "faceDown", GameState::kHandSize);
  if (!face_down.ok()) return face_down.status();
  auto ready = readBool(object, "ready");
  if (!ready.ok()) return ready.status();
  return Player{*std::move(id), *std::move(hand), *std::move(face_up), *std::move(face_down),
                *ready};
}

}  // namespace

std::string serializeGameState(const GameState& state) {
  json players = json::array();
  for (const Player& player : state.getPlayers()) players.push_back(playerToJson(player));
  json finished = json::array();
  for (const std::string& player_id : state.getFinished()) finished.push_back(sanitized(player_id));
  json serialized{
      {"v", kSchemaVersion},
      {"drawPile", cardsToJson(state.getDrawPile())},
      {"pile", cardsToJson(state.getPile())},
      {"whoseTurn", state.getWhoseTurn()},
      {"phase", phaseName(state.getPhase())},
      {"finished", std::move(finished)},
      {"players", std::move(players)},
  };
  // Absent rather than null until the first play: rows from before the
  // field read the same way.
  if (const auto& play = state.getLastPlay(); play.has_value()) {
    serialized["lastPlay"] = json{
        {"player", sanitized(play->playerId)},
        {"cards", cardsToJson(play->cards)},
        {"burned", play->burned},
    };
  }
  return serialized.dump(/*indent=*/-1, /*indent_char=*/' ', /*ensure_ascii=*/false,
                         json::error_handler_t::replace);
}

absl::StatusOr<GameState> deserializeGameState(const std::string& serialized) {
  const json parsed = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return absl::InvalidArgumentError("not a JSON object");
  }
  auto version = readIntInRange(parsed, "v", kSchemaVersion, kSchemaVersion);
  if (!version.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown schema version: ", version.status().message()));
  }
  auto draw_pile = readCardCodes(parsed, "drawPile", 52);
  if (!draw_pile.ok()) return draw_pile.status();
  auto pile = readCardCodes(parsed, "pile", 52);
  if (!pile.ok()) return pile.status();
  auto phase = readPhase(parsed);
  if (!phase.ok()) return phase.status();

  if (!parsed.contains("finished") || !parsed["finished"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'finished'");
  }
  std::vector<std::string> finished;
  for (const json& entry : parsed["finished"]) {
    if (!entry.is_string()) return absl::InvalidArgumentError("finished holds player ids");
    finished.push_back(entry.get<std::string>());
  }

  if (!parsed.contains("players") || !parsed["players"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'players'");
  }
  std::vector<Player> players;
  for (const json& entry : parsed["players"]) {
    auto player = readPlayer(entry);
    if (!player.ok()) return player.status();
    players.push_back(*std::move(player));
  }
  if (players.empty()) return absl::InvalidArgumentError("no players");
  // whoseTurn indexes the roster while play is on; kNoTurn is setup's
  // and the end's. A playing row with no turn would wedge every seat on
  // "not your turn", so it is a dropped row, not a table.
  auto whose_turn =
      readIntInRange(parsed, "whoseTurn", *phase == Phase::Playing ? 0 : GameState::kNoTurn,
                     static_cast<int64_t>(players.size()) - 1);
  if (!whose_turn.ok()) return whose_turn.status();

  std::optional<LastPlay> last_play;
  if (parsed.contains("lastPlay")) {
    const json& play = parsed["lastPlay"];
    if (!play.is_object()) return absl::InvalidArgumentError("expected object field 'lastPlay'");
    auto player = readString(play, "player");
    if (!player.ok()) return player.status();
    auto cards = readCardCodes(play, "cards", 4);
    if (!cards.ok()) return cards.status();
    if (cards->empty()) return absl::InvalidArgumentError("a play holds cards");
    auto burned = readBool(play, "burned");
    if (!burned.ok()) return burned.status();
    last_play = LastPlay{*std::move(player), *std::move(cards), *burned};
  }

  return GameState{std::deque<Card>(draw_pile->begin(), draw_pile->end()),
                   *std::move(pile),
                   std::move(players),
                   *whose_turn,
                   *phase,
                   std::move(finished),
                   /*_gameId=*/"",
                   /*_versionId=*/"",
                   std::move(last_play)};
}

}  // namespace castle
