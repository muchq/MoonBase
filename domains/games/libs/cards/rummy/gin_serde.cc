#include "domains/games/libs/cards/rummy/gin_serde.h"

#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

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

// postgres jsonb rejects a NUL byte: U+FFFD, as the other rows do.
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

template <typename Enum>
struct Names {
  Enum value;
  const char* name;
};

constexpr Names<GinStage> kStages[] = {{GinStage::Upcard, "upcard"},
                                       {GinStage::StockOnly, "stock"},
                                       {GinStage::Draw, "draw"},
                                       {GinStage::Play, "play"}};
constexpr Names<Phase> kPhases[] = {
    {Phase::Playing, "playing"}, {Phase::Over, "over"}, {Phase::Abandoned, "abandoned"}};
constexpr Names<GinMoveKind> kKinds[] = {{GinMoveKind::Pass, "pass"},
                                         {GinMoveKind::DrawStock, "drawStock"},
                                         {GinMoveKind::DrawDiscard, "drawDiscard"},
                                         {GinMoveKind::Discard, "discard"},
                                         {GinMoveKind::Knock, "knock"}};
constexpr Names<GinEnding> kEndings[] = {{GinEnding::Knock, "knock"},
                                         {GinEnding::Gin, "gin"},
                                         {GinEnding::Undercut, "undercut"},
                                         {GinEnding::Draw, "draw"}};

template <typename Enum, std::size_t N>
const char* nameOf(const Names<Enum> (&names)[N], Enum value) {
  for (const auto& entry : names) {
    if (entry.value == value) return entry.name;
  }
  return names[0].name;
}

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

template <typename Enum, std::size_t N>
absl::StatusOr<Enum> readName(const json& object, const char* key, const Names<Enum> (&names)[N]) {
  auto name = readString(object, key);
  if (!name.ok()) return name.status();
  for (const auto& entry : names) {
    if (*name == entry.name) return entry.value;
  }
  return absl::InvalidArgumentError(absl::StrCat("unknown ", key));
}

absl::StatusOr<std::vector<Card>> readCards(const json& value, const char* what) {
  if (!value.is_array() || value.size() > 52) {
    return absl::InvalidArgumentError(absl::StrCat("expected cards in '", what, "'"));
  }
  std::vector<Card> cards;
  for (const json& code : value) {
    if (!code.is_number_integer() || code.get<int64_t>() < 0 || code.get<int64_t>() > 51) {
      return absl::InvalidArgumentError(absl::StrCat("card code out of range in '", what, "'"));
    }
    cards.emplace_back(static_cast<int>(code.get<int64_t>()));
  }
  return cards;
}

absl::StatusOr<std::vector<Card>> readCardField(const json& object, const char* key) {
  if (!object.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat("expected array field '", key, "'"));
  }
  return readCards(object[key], key);
}

json arrangementToJson(const Arrangement& hand) {
  json melds = json::array();
  for (const auto& meld : hand.melds) melds.push_back(cardsToJson(meld));
  return json{{"melds", std::move(melds)},
              {"deadwood", cardsToJson(hand.deadwood)},
              {"deadwoodPoints", hand.deadwoodPoints}};
}

absl::StatusOr<Arrangement> readArrangement(const json& object) {
  if (!object.is_object() || !object.contains("melds") || !object["melds"].is_array()) {
    return absl::InvalidArgumentError("expected an arranged hand");
  }
  Arrangement hand;
  for (const json& meld : object["melds"]) {
    auto cards = readCards(meld, "melds");
    if (!cards.ok()) return cards.status();
    hand.melds.push_back(*std::move(cards));
  }
  auto deadwood = readCardField(object, "deadwood");
  if (!deadwood.ok()) return deadwood.status();
  hand.deadwood = *std::move(deadwood);
  auto points = readIntInRange(object, "deadwoodPoints", 0, 520);
  if (!points.ok()) return points.status();
  hand.deadwoodPoints = *points;
  return hand;
}

}  // namespace

std::string serializeGinState(const GinState& state) {
  json players = json::array();
  for (const Player& seat : state.getPlayers()) {
    players.push_back(json{{"id", sanitized(seat.id)}, {"hand", cardsToJson(seat.hand)}});
  }
  json serialized{
      {"v", kSchemaVersion},
      {"stock", cardsToJson(state.getStock())},
      {"discard", cardsToJson(state.getDiscard())},
      {"players", std::move(players)},
      {"whoseTurn", state.getWhoseTurn()},
      {"stage", nameOf(kStages, state.getStage())},
      {"phase", nameOf(kPhases, state.getPhase())},
      {"upcardPasses", state.getUpcardPasses()},
  };
  if (state.getTakenDiscard().has_value()) {
    serialized["takenDiscard"] = state.getTakenDiscard()->intValue();
  }
  if (const auto& move = state.getLastMove(); move.has_value()) {
    serialized["lastMove"] = json{{"player", sanitized(move->playerId)},
                                  {"kind", nameOf(kKinds, move->kind)},
                                  {"cards", cardsToJson(move->cards)}};
  }
  if (const auto& result = state.getResult(); result.has_value()) {
    json hands = json::array();
    for (const Arrangement& hand : result->hands) hands.push_back(arrangementToJson(hand));
    serialized["result"] = json{{"ending", nameOf(kEndings, result->ending)},
                                {"knocker", result->knocker},
                                {"winner", result->winner},
                                {"points", result->points},
                                {"laidOff", cardsToJson(result->laidOff)},
                                {"hands", std::move(hands)}};
  }
  return serialized.dump(/*indent=*/-1, /*indent_char=*/' ', /*ensure_ascii=*/false,
                         json::error_handler_t::replace);
}

absl::StatusOr<GinState> deserializeGinState(const std::string& serialized) {
  const json parsed = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return absl::InvalidArgumentError("not a JSON object");
  }
  auto version = readIntInRange(parsed, "v", kSchemaVersion, kSchemaVersion);
  if (!version.ok()) return version.status();

  auto stock = readCardField(parsed, "stock");
  if (!stock.ok()) return stock.status();
  auto discard = readCardField(parsed, "discard");
  if (!discard.ok()) return discard.status();
  if (!parsed.contains("players") || !parsed["players"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'players'");
  }
  std::vector<Player> players;
  for (const json& seat : parsed["players"]) {
    if (!seat.is_object()) return absl::InvalidArgumentError("a seat is an object");
    auto id = readString(seat, "id");
    if (!id.ok()) return id.status();
    auto hand = readCardField(seat, "hand");
    if (!hand.ok()) return hand.status();
    players.push_back(Player{*std::move(id), *std::move(hand)});
  }
  auto stage = readName(parsed, "stage", kStages);
  if (!stage.ok()) return stage.status();
  auto phase = readName(parsed, "phase", kPhases);
  if (!phase.ok()) return phase.status();
  auto passes = readIntInRange(parsed, "upcardPasses", 0, 1);
  if (!passes.ok()) return passes.status();

  const int seats = static_cast<int>(players.size());
  // Only an abandoned deal has fewer than two seats: a leave is what
  // abandons it.
  if (*phase == Phase::Abandoned ? seats > GinState::kSeats : seats != GinState::kSeats) {
    return absl::InvalidArgumentError("gin seats two");
  }
  const bool playing = *phase == Phase::Playing;
  auto turn = readIntInRange(parsed, "whoseTurn", playing ? 0 : GinState::kNoTurn,
                             playing ? seats - 1 : GinState::kNoTurn);
  if (!turn.ok()) return turn.status();

  std::optional<Card> taken;
  if (parsed.contains("takenDiscard")) {
    auto card = readIntInRange(parsed, "takenDiscard", 0, 51);
    if (!card.ok()) return card.status();
    taken.emplace(*card);
  }
  std::optional<GinLastMove> lastMove;
  if (parsed.contains("lastMove")) {
    const json& move = parsed["lastMove"];
    if (!move.is_object()) return absl::InvalidArgumentError("expected object 'lastMove'");
    auto player = readString(move, "player");
    if (!player.ok()) return player.status();
    auto kind = readName(move, "kind", kKinds);
    if (!kind.ok()) return kind.status();
    auto cards = readCardField(move, "cards");
    if (!cards.ok()) return cards.status();
    lastMove = GinLastMove{*std::move(player), *kind, *std::move(cards)};
  }
  std::optional<GinResult> result;
  if (parsed.contains("result")) {
    const json& ended = parsed["result"];
    if (!ended.is_object()) return absl::InvalidArgumentError("expected object 'result'");
    GinResult read;
    auto ending = readName(ended, "ending", kEndings);
    if (!ending.ok()) return ending.status();
    read.ending = *ending;
    auto knocker = readIntInRange(ended, "knocker", -1, seats - 1);
    if (!knocker.ok()) return knocker.status();
    read.knocker = *knocker;
    auto winner = readIntInRange(ended, "winner", -1, seats - 1);
    if (!winner.ok()) return winner.status();
    read.winner = *winner;
    auto points = readIntInRange(ended, "points", 0, 1000);
    if (!points.ok()) return points.status();
    read.points = *points;
    auto laidOff = readCardField(ended, "laidOff");
    if (!laidOff.ok()) return laidOff.status();
    read.laidOff = *std::move(laidOff);
    if (!ended.contains("hands") || !ended["hands"].is_array() ||
        static_cast<int>(ended["hands"].size()) != seats) {
      return absl::InvalidArgumentError("expected one arranged hand per seat");
    }
    for (const json& hand : ended["hands"]) {
      auto arranged = readArrangement(hand);
      if (!arranged.ok()) return arranged.status();
      read.hands.push_back(*std::move(arranged));
    }
    result = std::move(read);
  }
  // Over by play is over with a result: its winner, or a draw.
  if ((*phase == Phase::Over) != result.has_value()) {
    return absl::InvalidArgumentError("a deal over by play has its result");
  }
  return GinState{std::deque<Card>(stock->begin(), stock->end()),
                  *std::move(discard),
                  std::move(players),
                  *turn,
                  *stage,
                  *phase,
                  *passes,
                  taken,
                  std::move(lastMove),
                  std::move(result),
                  "",
                  ""};
}

}  // namespace rummy
