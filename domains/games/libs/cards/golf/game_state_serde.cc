#include "domains/games/libs/cards/golf/game_state_serde.h"

#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 1;

template <typename Cards>
json cardsToJson(const Cards& cards) {
  json codes = json::array();
  for (const Card& card : cards) codes.push_back(card.intValue());
  return codes;
}

// A NUL byte is valid JSON (escapes to \u0000) but postgres jsonb — the
// step-2 storage column — rejects it, so a NUL in a player-supplied name
// gets the same treatment as invalid UTF-8: U+FFFD, and the write path
// outlives the weirdest name.
std::optional<std::string> sanitizedName(const std::optional<std::string>& name) {
  if (!name.has_value()) return std::nullopt;
  std::string safe;
  for (const char c : *name) {
    if (c == '\0') {
      safe += "\xEF\xBF\xBD";  // U+FFFD
    } else {
      safe += c;
    }
  }
  return safe;
}

json playerToJson(const Player& player) {
  json peeked = json::array();
  for (const Position position : player.getPeeked()) peeked.push_back(indexOfPosition(position));
  return json{
      {"name", sanitizedName(player.getName())},
      {"cards", cardsToJson(player.allCards())},
      {"peeked", std::move(peeked)},
      {"donePeeking", player.hasCompletedPeeks()},
  };
}

// Field accessors that turn shape errors into invalid-argument statuses.
// The rule the no-throw guarantee rests on: no get<>() runs before an
// is_*() check of the same value. Integers read as int64 and range-check
// before narrowing — get<int>() alone is a static_cast, so an attacker's
// 2^32+1 would otherwise wrap to 1 and walk through every gate below.

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

absl::StatusOr<std::deque<Card>> readCardCodes(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_array()) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  std::deque<Card> cards;
  for (const json& code : object[key]) {
    if (!code.is_number_integer() || code.get<int64_t>() < 0 || code.get<int64_t>() > 51) {
      return absl::InvalidArgumentError(absl::StrCat("card code out of range in '", key, "'"));
    }
    cards.emplace_back(static_cast<int>(code.get<int64_t>()));
  }
  return cards;
}

absl::StatusOr<Player> readPlayer(const json& object) {
  if (!object.is_object()) return absl::InvalidArgumentError("expected player object");

  std::optional<std::string> name;
  if (!object.contains("name") || (!object["name"].is_null() && !object["name"].is_string())) {
    return absl::InvalidArgumentError("expected string-or-null field 'name'");
  }
  if (object["name"].is_string()) name = object["name"].get<std::string>();

  auto hand = readCardCodes(object, "cards");
  if (!hand.ok()) return hand.status();
  if (hand->size() != 4) return absl::InvalidArgumentError("a hand is exactly four cards");

  if (!object.contains("peeked") || !object["peeked"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'peeked'");
  }
  std::vector<Position> peeked;
  for (const json& index : object["peeked"]) {
    const std::optional<Position> position =
        index.is_number_integer() ? positionFromIndex(static_cast<int>(index.get<int64_t>()))
                                  : std::nullopt;
    if (!position.has_value()) {
      return absl::InvalidArgumentError("peek position out of range");
    }
    peeked.push_back(*position);
  }

  auto done_peeking = readBool(object, "donePeeking");
  if (!done_peeking.ok()) return done_peeking.status();

  return Player{std::move(name), (*hand)[0],        (*hand)[1],   (*hand)[2],
                (*hand)[3],      std::move(peeked), *done_peeking};
}

}  // namespace

std::string serializeGameState(const GameState& state) {
  json players = json::array();
  for (const Player& player : state.getPlayers()) players.push_back(playerToJson(player));
  const json serialized{
      {"v", kSchemaVersion},
      {"drawPile", cardsToJson(state.getDrawPile())},
      {"discardPile", cardsToJson(state.getDiscardPile())},
      {"peekedAtDrawPile", state.getPeekedAtDrawPile()},
      {"whoseTurn", state.getWhoseTurn()},
      {"whoKnocked", state.getWhoKnocked()},
      {"peeksHidden", state.getPeeksHidden()},
      {"players", std::move(players)},
  };
  // Player names are player-supplied; strict dump() would throw on a name
  // that is not valid UTF-8. Replacement (U+FFFD) keeps the write path
  // alive and stays deterministic.
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

  auto draw_pile = readCardCodes(parsed, "drawPile");
  if (!draw_pile.ok()) return draw_pile.status();
  auto discard_pile = readCardCodes(parsed, "discardPile");
  if (!discard_pile.ok()) return discard_pile.status();
  auto peeked_at_draw_pile = readBool(parsed, "peekedAtDrawPile");
  if (!peeked_at_draw_pile.ok()) return peeked_at_draw_pile.status();
  auto peeks_hidden = readBool(parsed, "peeksHidden");
  if (!peeks_hidden.ok()) return peeks_hidden.status();

  if (!parsed.contains("players") || !parsed["players"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'players'");
  }
  std::vector<Player> players;
  for (const json& entry : parsed["players"]) {
    auto player = readPlayer(entry);
    if (!player.ok()) return player.status();
    players.push_back(*std::move(player));
  }
  // The engine's turn arithmetic divides by the roster size, and seat
  // fields index into it; both need the roster known before they read.
  if (players.empty()) return absl::InvalidArgumentError("no players");
  const int64_t seats = static_cast<int64_t>(players.size());
  auto whose_turn = readIntInRange(parsed, "whoseTurn", 0, seats - 1);
  if (!whose_turn.ok()) return whose_turn.status();
  auto who_knocked = readIntInRange(parsed, "whoKnocked", -1, seats - 1);
  if (!who_knocked.ok()) return who_knocked.status();

  return GameState{*std::move(draw_pile),
                   *std::move(discard_pile),
                   std::move(players),
                   /*_peekedAtDrawPile=*/*peeked_at_draw_pile,
                   /*_whoseTurn=*/*whose_turn,
                   /*_whoKnocked=*/*who_knocked,
                   /*_peeksHidden=*/*peeks_hidden,
                   /*_gameId=*/"",
                   /*_version_id=*/""};
}

}  // namespace golf
