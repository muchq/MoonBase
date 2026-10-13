#include "domains/games/libs/cards/spades/table_serde.h"

#include <array>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace spades {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 1;
constexpr int kDeckSize = kSeats * kHandSize;

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

json PlaysJson(const std::vector<TrickPlay>& plays) {
  json out = json::array();
  for (const TrickPlay& play : plays)
    out.push_back({{"seat", play.seat}, {"card", play.card.intValue()}});
  return out;
}

template <std::size_t N>
json Counts(const std::array<int, N>& counts) {
  json out = json::array();
  for (int count : counts) out.push_back(count);
  return out;
}

absl::StatusOr<const json*> Field(const json& payload, const char* name) {
  if (!payload.is_object() || !payload.contains(name)) {
    return absl::InvalidArgumentError(absl::StrCat("missing ", name));
  }
  return &payload[name];
}

absl::StatusOr<int> Int(const json& value, const char* name) {
  // A count past an int's range is refused, not narrowed.
  // Parsed, a count at zero or above is unsigned and one below signed.
  const bool fits =
      value.is_number_unsigned()
          ? value.get<uint64_t>() <= std::numeric_limits<int>::max()
          : value.is_number_integer() && value.get<int64_t>() >= std::numeric_limits<int>::min();
  if (!fits) return absl::InvalidArgumentError(absl::StrCat(name, " is a count"));
  return value.get<int>();
}

absl::StatusOr<int> IntField(const json& payload, const char* name) {
  const auto field = Field(payload, name);
  if (!field.ok()) return field.status();
  return Int(**field, name);
}

template <std::size_t N>
absl::StatusOr<std::array<int, N>> CountsField(const json& payload, const char* name) {
  const auto field = Field(payload, name);
  if (!field.ok()) return field.status();
  if (!(*field)->is_array() || (*field)->size() != N) {
    return absl::InvalidArgumentError(absl::StrCat(name, " is ", N, " counts"));
  }
  std::array<int, N> counts{};
  for (std::size_t i = 0; i < N; ++i) {
    const auto count = Int((**field)[i], name);
    if (!count.ok()) return count.status();
    counts[i] = *count;
  }
  return counts;
}

absl::StatusOr<Card> CardOf(const json& value) {
  if (!value.is_number_integer() || value.get<int>() < 0 || value.get<int>() >= kDeckSize) {
    return absl::InvalidArgumentError("a card is 0 to 51");
  }
  return Card(value.get<int>());
}

absl::StatusOr<std::vector<TrickPlay>> PlaysOf(const json& value) {
  if (!value.is_array()) return absl::InvalidArgumentError("plays are an array");
  std::vector<TrickPlay> plays;
  for (const json& play : value) {
    const auto seat = IntField(play, "seat");
    if (!seat.ok()) return seat.status();
    const auto card_field = Field(play, "card");
    if (!card_field.ok()) return card_field.status();
    const auto card = CardOf(**card_field);
    if (!card.ok()) return card.status();
    plays.push_back({*seat, *card});
  }
  return plays;
}

absl::StatusOr<std::optional<int>> OptionalInt(const json& value, const char* name) {
  if (value.is_null()) return std::optional<int>();
  const auto count = Int(value, name);
  if (!count.ok()) return count.status();
  return std::optional<int>(*count);
}

absl::StatusOr<Hand> HandOf(const json& payload) {
  const auto dealer = IntField(payload, "dealer");
  if (!dealer.ok()) return dealer.status();

  const auto hands_field = Field(payload, "hands");
  if (!hands_field.ok()) return hands_field.status();
  if (!(*hands_field)->is_array() || (*hands_field)->size() != kSeats) {
    return absl::InvalidArgumentError("hands are four");
  }
  std::array<std::vector<Card>, kSeats> hands;
  for (int seat = 0; seat < kSeats; ++seat) {
    const json& held = (**hands_field)[seat];
    if (!held.is_array()) return absl::InvalidArgumentError("a hand is an array");
    for (const json& value : held) {
      const auto card = CardOf(value);
      if (!card.ok()) return card.status();
      hands[seat].push_back(*card);
    }
  }

  const auto bids_field = Field(payload, "bids");
  if (!bids_field.ok()) return bids_field.status();
  if (!(*bids_field)->is_array() || (*bids_field)->size() != kSeats) {
    return absl::InvalidArgumentError("bids are four");
  }
  std::array<std::optional<int>, kSeats> bids;
  for (int seat = 0; seat < kSeats; ++seat) {
    const auto bid = OptionalInt((**bids_field)[seat], "a bid");
    if (!bid.ok()) return bid.status();
    bids[seat] = *bid;
  }

  const auto tricks = CountsField<kSeats>(payload, "tricks");
  if (!tricks.ok()) return tricks.status();
  const auto current_field = Field(payload, "current");
  if (!current_field.ok()) return current_field.status();
  auto current = PlaysOf(**current_field);
  if (!current.ok()) return current.status();

  const auto last_field = Field(payload, "last");
  if (!last_field.ok()) return last_field.status();
  std::optional<Trick> last;
  if (!(*last_field)->is_null()) {
    const auto plays_field = Field(**last_field, "plays");
    if (!plays_field.ok()) return plays_field.status();
    auto plays = PlaysOf(**plays_field);
    if (!plays.ok()) return plays.status();
    const auto winner = IntField(**last_field, "winner");
    if (!winner.ok()) return winner.status();
    last = Trick{*std::move(plays), *winner};
  }

  const auto broken_field = Field(payload, "spadesBroken");
  if (!broken_field.ok()) return broken_field.status();
  if (!(*broken_field)->is_boolean()) return absl::InvalidArgumentError("spadesBroken is a bool");

  return Hand::restore(*dealer, std::move(hands), bids, *tricks, *std::move(current),
                       std::move(last), (*broken_field)->get<bool>());
}

absl::StatusOr<std::vector<GameScore>> GamesOf(const json& payload) {
  const auto field = Field(payload, "games");
  if (!field.ok()) return field.status();
  if (!(*field)->is_array()) return absl::InvalidArgumentError("games is an array");
  std::vector<GameScore> games;
  for (const json& game : **field) {
    const auto winner_field = Field(game, "winner");
    if (!winner_field.ok()) return winner_field.status();
    const auto winner = OptionalInt(**winner_field, "a winner");
    if (!winner.ok()) return winner.status();
    const auto scores = CountsField<kTeams>(game, "scores");
    if (!scores.ok()) return scores.status();
    games.push_back({*winner, *scores});
  }
  return games;
}

absl::StatusOr<std::vector<HandLine>> SheetOf(const json& payload) {
  const auto field = Field(payload, "lines");
  if (!field.ok()) return field.status();
  if (!(*field)->is_array()) return absl::InvalidArgumentError("lines is an array");
  std::vector<HandLine> sheet;
  for (const json& line : **field) {
    const auto bids = CountsField<kSeats>(line, "bids");
    if (!bids.ok()) return bids.status();
    const auto tricks = CountsField<kSeats>(line, "tricks");
    if (!tricks.ok()) return tricks.status();
    const auto points = CountsField<kTeams>(line, "points");
    if (!points.ok()) return points.status();
    const auto scores = CountsField<kTeams>(line, "scores");
    if (!scores.ok()) return scores.status();
    sheet.push_back({*bids, *tricks, *points, *scores});
  }
  return sheet;
}

}  // namespace

std::string serializeTable(const Table& table) {
  const Hand& hand = table.hand();
  json seats = json::array();
  for (const std::string& id : table.seats()) seats.push_back(Sanitized(id));
  json hands = json::array();
  for (const auto& held : hand.hands()) {
    json cards = json::array();
    for (const Card& card : held) cards.push_back(card.intValue());
    hands.push_back(std::move(cards));
  }
  json bids = json::array();
  for (const auto& bid : hand.bids()) bids.push_back(bid.has_value() ? json(*bid) : json(nullptr));
  json last = nullptr;
  if (hand.last().has_value()) {
    last = {{"plays", PlaysJson(hand.last()->plays)}, {"winner", hand.last()->winner}};
  }
  json games = json::array();
  for (const GameScore& game : table.games()) {
    games.push_back({{"winner", game.winner.has_value() ? json(*game.winner) : json(nullptr)},
                     {"scores", Counts(game.scores)}});
  }
  json sheet = json::array();
  for (const HandLine& line : table.hands()) {
    sheet.push_back({{"bids", Counts(line.bids)},
                     {"tricks", Counts(line.tricks)},
                     {"points", Counts(line.points)},
                     {"scores", Counts(line.scores)}});
  }
  const json row{{"v", kSchemaVersion},
                 {"seats", std::move(seats)},
                 {"dealer", hand.dealer()},
                 {"hands", std::move(hands)},
                 {"bids", std::move(bids)},
                 {"tricks", Counts(hand.tricks())},
                 {"current", PlaysJson(hand.current())},
                 {"last", std::move(last)},
                 {"spadesBroken", hand.spadesBroken()},
                 {"handNumber", table.handNumber()},
                 {"scores", Counts(table.scores())},
                 {"bags", Counts(table.bags())},
                 {"lines", std::move(sheet)},
                 {"games", std::move(games)},
                 {"left", table.left().has_value() ? json(*table.left()) : json(nullptr)}};
  return row.dump();
}

absl::StatusOr<Table> deserializeTable(const std::string& serialized) {
  const json payload = json::parse(serialized, nullptr, /*allow_exceptions=*/false);
  if (payload.is_discarded() || !payload.is_object()) {
    return absl::InvalidArgumentError("a spades row is a JSON object");
  }
  const auto version = Field(payload, "v");
  if (!version.ok()) return version.status();
  if (**version != kSchemaVersion) {
    return absl::InvalidArgumentError(absl::StrCat("unknown spades schema: ", (*version)->dump()));
  }
  const auto seats_field = Field(payload, "seats");
  if (!seats_field.ok()) return seats_field.status();
  if (!(*seats_field)->is_array()) return absl::InvalidArgumentError("seats is an array");
  std::vector<std::string> seats;
  for (const json& id : **seats_field) {
    if (!id.is_string()) return absl::InvalidArgumentError("a seat is a name");
    seats.push_back(id.get<std::string>());
  }
  auto hand = HandOf(payload);
  if (!hand.ok()) return hand.status();
  const auto hand_number = IntField(payload, "handNumber");
  if (!hand_number.ok()) return hand_number.status();
  const auto scores = CountsField<kTeams>(payload, "scores");
  if (!scores.ok()) return scores.status();
  const auto bags = CountsField<kTeams>(payload, "bags");
  if (!bags.ok()) return bags.status();
  auto sheet = SheetOf(payload);
  if (!sheet.ok()) return sheet.status();
  auto games = GamesOf(payload);
  if (!games.ok()) return games.status();
  const auto left_field = Field(payload, "left");
  if (!left_field.ok()) return left_field.status();
  const auto left = OptionalInt(**left_field, "left");
  if (!left.ok()) return left.status();
  return Table::restore(std::move(seats), *std::move(hand), *hand_number, *scores, *bags,
                        *std::move(sheet), *std::move(games), *left);
}

}  // namespace spades
