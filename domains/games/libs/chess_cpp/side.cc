#include "domains/games/libs/chess_cpp/side.h"

#include <string_view>

#include "chess.hpp"

namespace chess_cpp {

std::string_view ToString(Side side) { return side == Side::kWhite ? "white" : "black"; }

Side FromColor(chess::Color color) {
  return color == chess::Color::WHITE ? Side::kWhite : Side::kBlack;
}

chess::Color ToColor(Side side) {
  return side == Side::kWhite ? chess::Color::WHITE : chess::Color::BLACK;
}

}  // namespace chess_cpp
