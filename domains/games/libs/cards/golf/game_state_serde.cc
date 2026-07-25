#include "domains/games/libs/cards/golf/game_state_serde.h"

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

json cardsToJson(const std::deque<Card>& pile) {
  json codes = json::array();
  for (const Card& card : pile) codes.push_back(card.intValue());
  return codes;
}

json playerToJson(const Player& player) {
  json hand = json::array();
  for (const Card& card : player.allCards()) hand.push_back(card.intValue());
  json peeked = json::array();
  for (const Position position : player.getPeeked()) peeked.push_back(indexOfPosition(position));
  return json{
      {"name", player.getName().has_value() ? json(*player.getName()) : json(nullptr)},
      {"cards", std::move(hand)},
      {"peeked", std::move(peeked)},
      {"donePeeking", player.hasCompletedPeeks()},
  };
}

// Field accessors that turn shape errors into invalid-argument statuses;
// every read goes through one of these so no malformed byte can throw.

absl::StatusOr<int> readInt(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_number_integer()) {
    return absl::InvalidArgumentError(absl::StrCat("expected integer field '", key, "'"));
  }
  return object[key].get<int>();
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

absl::StatusOr<std::deque<Card>> readPile(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_array()) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  std::deque<Card> pile;
  for (const json& code : object[key]) {
    if (!code.is_number_integer() || code.get<int>() < 0 || code.get<int>() > 51) {
      return absl::InvalidArgumentError(absl::StrCat("card code out of range in '", key, "'"));
    }
    pile.emplace_back(code.get<int>());
  }
  return pile;
}

absl::StatusOr<Player> readPlayer(const json& object) {
  if (!object.is_object()) return absl::InvalidArgumentError("expected player object");

  std::optional<std::string> name;
  if (!object.contains("name") || (!object["name"].is_null() && !object["name"].is_string())) {
    return absl::InvalidArgumentError("expected string-or-null field 'name'");
  }
  if (object["name"].is_string()) name = object["name"].get<std::string>();

  auto hand = readPile(object, "cards");
  if (!hand.ok()) return hand.status();
  if (hand->size() != 4) return absl::InvalidArgumentError("a hand is exactly four cards");

  if (!object.contains("peeked") || !object["peeked"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'peeked'");
  }
  std::vector<Position> peeked;
  for (const json& index : object["peeked"]) {
    const std::optional<Position> position =
        index.is_number_integer() ? positionFromIndex(index.get<int>()) : std::nullopt;
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
      {"gameId", state.getGameId()},
      {"versionId", state.getVersionId()},
      {"drawPile", cardsToJson(state.getDrawPile())},
      {"discardPile", cardsToJson(state.getDiscardPile())},
      {"peekedAtDrawPile", state.getPeekedAtDrawPile()},
      {"whoseTurn", state.getWhoseTurn()},
      {"whoKnocked", state.getWhoKnocked()},
      {"peeksHidden", state.getPeeksHidden()},
      {"players", std::move(players)},
  };
  return serialized.dump();
}

absl::StatusOr<GameState> deserializeGameState(const std::string& serialized) {
  const json parsed = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return absl::InvalidArgumentError("not a JSON object");
  }

  auto version = readInt(parsed, "v");
  if (!version.ok()) return version.status();
  if (*version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown schema version ", *version));
  }

  auto game_id = readString(parsed, "gameId");
  if (!game_id.ok()) return game_id.status();
  auto version_id = readString(parsed, "versionId");
  if (!version_id.ok()) return version_id.status();
  auto draw_pile = readPile(parsed, "drawPile");
  if (!draw_pile.ok()) return draw_pile.status();
  auto discard_pile = readPile(parsed, "discardPile");
  if (!discard_pile.ok()) return discard_pile.status();
  auto peeked_at_draw_pile = readBool(parsed, "peekedAtDrawPile");
  if (!peeked_at_draw_pile.ok()) return peeked_at_draw_pile.status();
  auto whose_turn = readInt(parsed, "whoseTurn");
  if (!whose_turn.ok()) return whose_turn.status();
  auto who_knocked = readInt(parsed, "whoKnocked");
  if (!who_knocked.ok()) return who_knocked.status();
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

  // The engine indexes seats by these; an out-of-range value from a
  // corrupted row must fail here, not in a .at() later.
  const int seats = static_cast<int>(players.size());
  if (*whose_turn < 0 || *whose_turn >= seats) {
    return absl::InvalidArgumentError("whoseTurn out of range");
  }
  if (*who_knocked < -1 || *who_knocked >= seats) {
    return absl::InvalidArgumentError("whoKnocked out of range");
  }

  return GameState{*std::move(draw_pile), *std::move(discard_pile),
                   std::move(players),    *peeked_at_draw_pile,
                   *whose_turn,           *who_knocked,
                   *peeks_hidden,         *std::move(game_id),
                   *std::move(version_id)};
}

}  // namespace golf
