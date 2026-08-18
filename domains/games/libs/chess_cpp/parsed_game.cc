#include "domains/games/libs/chess_cpp/parsed_game.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chess_cpp {

void Headers::Add(std::string name, std::string value) {
  entries_.emplace_back(std::move(name), std::move(value));
}

std::optional<std::string_view> Headers::Get(std::string_view name) const {
  for (const auto& [key, value] : entries_) {
    if (key == name) return value;
  }
  return std::nullopt;
}

}  // namespace chess_cpp
