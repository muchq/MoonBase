#include "domains/games/libs/cards/rummy/game_state_serde.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/card.h"

namespace rummy {
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
    case Phase::Playing:
      return "playing";
    case Phase::Over:
      return "over";
    case Phase::Abandoned:
      return "abandoned";
  }
  return "playing";
}

const char* stageName(Stage stage) { return stage == Stage::Draw ? "draw" : "play"; }

const char* kindName(MoveKind kind) {
  switch (kind) {
    case MoveKind::DrawStock:
      return "drawStock";
    case MoveKind::DrawDiscard:
      return "drawDiscard";
    case MoveKind::Meld:
      return "meld";
    case MoveKind::LayOff:
      return "layOff";
    case MoveKind::Discard:
      return "discard";
  }
  return "drawStock";
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

absl::StatusOr<std::string> readString(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_string()) {
    return absl::InvalidArgumentError(absl::StrCat("expected string field '", key, "'"));
  }
  return object[key].get<std::string>();
}

absl::StatusOr<Card> readCard(const json& object, const char* key) {
  auto code = readIntInRange(object, key, 0, 51);
  if (!code.ok()) return code.status();
  return Card(*code);
}

absl::StatusOr<std::vector<Card>> readCardCodes(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_array()) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  if (object[key].size() > 52) {
    return absl::InvalidArgumentError(absl::StrCat("too many cards in '", key, "'"));
  }
  std::vector<Card> cards;
  for (const json& code : object[key]) {
    if (!code.is_number_integer() || code.get<int64_t>() < 0 || code.get<int64_t>() > 51) {
      return absl::InvalidArgumentError(absl::StrCat("card code out of range in '", key, "'"));
    }
    cards.emplace_back(static_cast<int>(code.get<int64_t>()));
  }
  return cards;
}

absl::StatusOr<Phase> readPhase(const json& object) {
  auto name = readString(object, "phase");
  if (!name.ok()) return name.status();
  if (*name == "playing") return Phase::Playing;
  if (*name == "over") return Phase::Over;
  if (*name == "abandoned") return Phase::Abandoned;
  return absl::InvalidArgumentError("unknown phase");
}

absl::StatusOr<Stage> readStage(const json& object) {
  auto name = readString(object, "stage");
  if (!name.ok()) return name.status();
  if (*name == "draw") return Stage::Draw;
  if (*name == "play") return Stage::Play;
  return absl::InvalidArgumentError("unknown stage");
}

absl::StatusOr<MoveKind> readKind(const json& object) {
  auto name = readString(object, "kind");
  if (!name.ok()) return name.status();
  for (MoveKind kind : {MoveKind::DrawStock, MoveKind::DrawDiscard, MoveKind::Meld,
                        MoveKind::LayOff, MoveKind::Discard}) {
    if (*name == kindName(kind)) return kind;
  }
  return absl::InvalidArgumentError("unknown move kind");
}

absl::StatusOr<std::vector<json>> readObjects(const json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_array()) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  std::vector<json> entries;
  for (const json& entry : object[key]) {
    if (!entry.is_object()) {
      return absl::InvalidArgumentError(absl::StrCat("expected objects in '", key, "'"));
    }
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace

std::string serializeGameState(const GameState& state) {
  json players = json::array();
  for (const Player& player : state.getPlayers()) {
    players.push_back(json{{"id", sanitized(player.id)}, {"hand", cardsToJson(player.hand)}});
  }
  json melds = json::array();
  for (const Meld& meld : state.getMelds()) {
    melds.push_back(json{{"owner", sanitized(meld.owner)}, {"cards", cardsToJson(meld.cards)}});
  }
  json serialized{
      {"v", kSchemaVersion},
      {"stock", cardsToJson(state.getStock())},
      {"discard", cardsToJson(state.getDiscard())},
      {"whoseTurn", state.getWhoseTurn()},
      {"stage", stageName(state.getStage())},
      {"phase", phaseName(state.getPhase())},
      {"players", std::move(players)},
      {"melds", std::move(melds)},
  };
  if (const auto& taken = state.getTakenDiscard(); taken.has_value()) {
    serialized["takenDiscard"] = taken->intValue();
  }
  if (const auto& owed = state.getMustPlay(); owed.has_value()) {
    serialized["mustPlay"] = owed->intValue();
  }
  if (const auto& move = state.getLastMove(); move.has_value()) {
    serialized["lastMove"] = json{
        {"player", sanitized(move->playerId)},
        {"kind", kindName(move->kind)},
        {"cards", cardsToJson(move->cards)},
        {"meld", move->meld},
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
  auto stock = readCardCodes(parsed, "stock");
  if (!stock.ok()) return stock.status();
  auto discard = readCardCodes(parsed, "discard");
  if (!discard.ok()) return discard.status();
  auto phase = readPhase(parsed);
  if (!phase.ok()) return phase.status();
  auto stage = readStage(parsed);
  if (!stage.ok()) return stage.status();

  auto player_entries = readObjects(parsed, "players");
  if (!player_entries.ok()) return player_entries.status();
  std::vector<Player> players;
  for (const json& entry : *player_entries) {
    auto id = readString(entry, "id");
    if (!id.ok()) return id.status();
    auto hand = readCardCodes(entry, "hand");
    if (!hand.ok()) return hand.status();
    players.push_back(Player{*std::move(id), *std::move(hand)});
  }
  if (players.empty()) return absl::InvalidArgumentError("no players");

  auto meld_entries = readObjects(parsed, "melds");
  if (!meld_entries.ok()) return meld_entries.status();
  std::vector<Meld> melds;
  for (const json& entry : *meld_entries) {
    auto owner = readString(entry, "owner");
    if (!owner.ok()) return owner.status();
    auto cards = readCardCodes(entry, "cards");
    if (!cards.ok()) return cards.status();
    melds.push_back(Meld{*std::move(owner), *std::move(cards)});
  }

  // whoseTurn indexes the roster while play is on; kNoTurn is the end's.
  // A playing row with no turn would wedge every seat on "not your turn",
  // so it is a dropped row, not a table.
  auto whose_turn =
      readIntInRange(parsed, "whoseTurn", *phase == Phase::Playing ? 0 : GameState::kNoTurn,
                     static_cast<int64_t>(players.size()) - 1);
  if (!whose_turn.ok()) return whose_turn.status();

  std::optional<Card> taken;
  if (parsed.contains("takenDiscard")) {
    auto card = readCard(parsed, "takenDiscard");
    if (!card.ok()) return card.status();
    taken = *card;
  }

  std::optional<LastMove> last_move;
  if (parsed.contains("lastMove")) {
    const json& move = parsed["lastMove"];
    if (!move.is_object()) return absl::InvalidArgumentError("expected object field 'lastMove'");
    auto player = readString(move, "player");
    if (!player.ok()) return player.status();
    auto kind = readKind(move);
    if (!kind.ok()) return kind.status();
    auto cards = readCardCodes(move, "cards");
    if (!cards.ok()) return cards.status();
    auto meld = readIntInRange(move, "meld", -1, static_cast<int64_t>(melds.size()) - 1);
    if (!meld.ok()) return meld.status();
    // The shape the engine writes for each kind: a stock draw names no
    // card, a discard or a lay-off names one, a draw from the discard one or
    // more (taken down into the pile), a meld three or more; only a meld or
    // a lay-off names a meld.
    const bool on_meld = *kind == MoveKind::Meld || *kind == MoveKind::LayOff;
    const std::size_t named = cards->size();
    const bool shaped = *kind == MoveKind::DrawStock     ? named == 0
                        : *kind == MoveKind::Meld        ? named >= 3
                        : *kind == MoveKind::DrawDiscard ? named >= 1
                                                         : named == 1;
    if (!shaped || on_meld != (*meld >= 0)) {
      return absl::InvalidArgumentError("lastMove does not have its kind's shape");
    }
    last_move = LastMove{*std::move(player), *kind, *std::move(cards), *meld};
  }

  // A card owed is the seat on turn's, mid-turn, and still in its hand.
  std::optional<Card> owed;
  if (parsed.contains("mustPlay")) {
    auto card = readCard(parsed, "mustPlay");
    if (!card.ok()) return card.status();
    const std::vector<Card>* hand = *phase == Phase::Playing && *stage == Stage::Play
                                        ? &players.at(static_cast<std::size_t>(*whose_turn)).hand
                                        : nullptr;
    if (hand == nullptr || std::find(hand->begin(), hand->end(), *card) == hand->end()) {
      return absl::InvalidArgumentError("a card owed that is not the seat on turn's to play");
    }
    owed = *card;
  }

  // A table in play has a move to make: a draw has somewhere to draw from,
  // and a seat mid-turn has a card to put down. A row without one would
  // hold every seat at "not your turn" until they all left.
  if (*phase == Phase::Playing) {
    const Player& on_turn = players.at(static_cast<std::size_t>(*whose_turn));
    const bool stuck =
        *stage == Stage::Draw ? stock->empty() && discard->empty() : on_turn.hand.empty();
    if (stuck) return absl::InvalidArgumentError("a playing row with no move to make");
  }

  return GameState{std::deque<Card>(stock->begin(), stock->end()),
                   *std::move(discard),
                   std::move(players),
                   std::move(melds),
                   *whose_turn,
                   *stage,
                   *phase,
                   taken,
                   /*_gameId=*/"",
                   /*_versionId=*/"",
                   std::move(last_move),
                   owed};
}

}  // namespace rummy
