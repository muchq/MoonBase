#include "domains/games/libs/cards/rummy/table_serde.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/game_state_serde.h"
#include "domains/games/libs/cards/rummy/gin_serde.h"

namespace rummy {
namespace {

using nlohmann::json;

constexpr int kSchemaVersion = 2;

// postgres jsonb rejects a NUL byte: U+FFFD, as the deal's serde does.
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

const char* phaseName(TablePhase phase) {
  switch (phase) {
    case TablePhase::Choosing:
      return "choosing";
    case TablePhase::Playing:
      return "playing";
    case TablePhase::Closed:
      return "closed";
  }
  return "closed";
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

// A lone deal, from before the table: that deal at a table of its seats.
absl::StatusOr<TableState> fromVersionOne(const std::string& serialized) {
  auto deal = deserializeGameState(serialized);
  if (!deal.ok()) return deal.status();
  std::vector<std::string> seats;
  for (const Player& player : deal->getPlayers()) seats.push_back(player.id);
  const int count = static_cast<int>(seats.size());
  const bool live = !deal->isOver();
  // The deal opened at the seat after its dealer's; mid-deal, the seat on
  // turn is the best record of it there is.
  const int dealer = live ? (deal->getWhoseTurn() + count - 1) % count : 0;
  return TableState{seats,
                    std::vector<int>(seats.size(), 0),
                    dealer,
                    1,
                    live ? TablePhase::Playing : TablePhase::Closed,
                    Variant::Basic,
                    *std::move(deal),
                    "",
                    ""};
}

}  // namespace

std::string serializeTableState(const TableState& table) {
  json seats = json::array();
  for (const std::string& seat : table.getSeats()) seats.push_back(sanitized(seat));
  json serialized{
      {"v", kSchemaVersion},
      {"phase", phaseName(table.getPhase())},
      {"seats", std::move(seats)},
      {"wins", table.getWins()},
      {"dealer", table.getDealer()},
      {"dealNumber", table.getDealNumber()},
      {"variant", std::string(variantName(table.getVariant()))},
  };
  // The deal in its own engine's form; the variant says which.
  if (const GameState* basic = table.basicDeal(); basic != nullptr) {
    serialized["deal"] = json::parse(serializeGameState(*basic));
  } else if (const GinState* gin = table.ginDeal(); gin != nullptr) {
    serialized["deal"] = json::parse(serializeGinState(*gin));
  }
  return serialized.dump(/*indent=*/-1, /*indent_char=*/' ', /*ensure_ascii=*/false,
                         json::error_handler_t::replace);
}

absl::StatusOr<TableState> deserializeTableState(const std::string& serialized) {
  const json parsed = json::parse(serialized, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return absl::InvalidArgumentError("not a JSON object");
  }
  if (parsed.contains("v") && parsed["v"] == 1) return fromVersionOne(serialized);
  auto version = readIntInRange(parsed, "v", kSchemaVersion, kSchemaVersion);
  if (!version.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown schema version: ", version.status().message()));
  }

  auto phase_name = readString(parsed, "phase");
  if (!phase_name.ok()) return phase_name.status();
  std::optional<TablePhase> phase;
  for (TablePhase candidate : {TablePhase::Choosing, TablePhase::Playing, TablePhase::Closed}) {
    if (*phase_name == phaseName(candidate)) phase = candidate;
  }
  if (!phase.has_value()) return absl::InvalidArgumentError("unknown phase");

  auto variant_name = readString(parsed, "variant");
  if (!variant_name.ok()) return variant_name.status();
  const std::optional<Variant> variant = parseVariant(*variant_name);
  if (!variant.has_value()) return absl::InvalidArgumentError("unknown variant");

  if (!parsed.contains("seats") || !parsed["seats"].is_array()) {
    return absl::InvalidArgumentError("expected array field 'seats'");
  }
  std::vector<std::string> seats;
  for (const json& seat : parsed["seats"]) {
    if (!seat.is_string()) return absl::InvalidArgumentError("seats hold player ids");
    seats.push_back(seat.get<std::string>());
  }
  const int count = static_cast<int>(seats.size());
  if (count > GameState::kMaxPlayers) return absl::InvalidArgumentError("too many seats");
  // Only a closed table has fewer than two: that is what closes it.
  if (*phase != TablePhase::Closed && count < GameState::kMinPlayers) {
    return absl::InvalidArgumentError("an open table seats two");
  }

  if (!parsed.contains("wins") || !parsed["wins"].is_array() ||
      parsed["wins"].size() != seats.size()) {
    return absl::InvalidArgumentError("expected one win count per seat");
  }
  std::vector<int> wins;
  for (const json& won : parsed["wins"]) {
    if (!won.is_number_integer() || won.get<int64_t>() < 0 || won.get<int64_t>() > 1'000'000) {
      return absl::InvalidArgumentError("win counts are counts");
    }
    wins.push_back(static_cast<int>(won.get<int64_t>()));
  }
  auto dealer = readIntInRange(parsed, "dealer", 0, count > 0 ? count - 1 : 0);
  if (!dealer.ok()) return dealer.status();
  auto deal_number = readIntInRange(parsed, "dealNumber", 0, 1'000'000);
  if (!deal_number.ok()) return deal_number.status();

  std::optional<Deal> deal;
  if (parsed.contains("deal")) {
    if (!parsed["deal"].is_object()) return absl::InvalidArgumentError("expected object 'deal'");
    if (*variant == Variant::Gin) {
      auto read = deserializeGinState(parsed["deal"].dump());
      if (!read.ok()) return read.status();
      deal.emplace(*std::move(read));
    } else {
      auto read = deserializeGameState(parsed["deal"].dump());
      if (!read.ok()) return read.status();
      deal.emplace(*std::move(read));
    }
  }
  // A deal is there exactly when one has been dealt.
  if (deal.has_value() != (*deal_number > 0)) {
    return absl::InvalidArgumentError("the deal count disagrees with the deal");
  }
  if (*phase == TablePhase::Playing) {
    if (!deal.has_value() || dealPhase(*deal) != Phase::Playing) {
      return absl::InvalidArgumentError("a playing table has a deal in play");
    }
    std::vector<std::string> dealt;
    for (const Player& player : dealPlayers(*deal)) dealt.push_back(player.id);
    if (dealt != seats) return absl::InvalidArgumentError("the deal seats the table");
  }
  if (*phase == TablePhase::Choosing && deal.has_value() && dealPhase(*deal) == Phase::Playing) {
    return absl::InvalidArgumentError("between deals, the last deal is over");
  }

  return TableState{std::move(seats),
                    std::move(wins),
                    *dealer,
                    *deal_number,
                    *phase,
                    *variant,
                    std::move(deal),
                    "",
                    ""};
}

}  // namespace rummy
