#ifndef DOMAINS_GAMES_LIBS_CHESS_CPP_SIDE_H
#define DOMAINS_GAMES_LIBS_CHESS_CPP_SIDE_H

#include <string_view>

#include "chess.hpp"

namespace chess_cpp {

/// Which side of the board something belongs to.
///
/// Deliberately not chess::Color: this is the spelling the indexer's DTOs
/// and the `motif_occurrences.side` column already use, and ToString below
/// is what gets written to a row. Keeping the persisted vocabulary ours
/// means the database does not inherit an upstream enum's opinions.
enum class Side { kWhite, kBlack };

/// "white" / "black" — the spelling the database column already uses.
std::string_view ToString(Side side);

Side FromColor(chess::Color color);
chess::Color ToColor(Side side);

/// The opposing side.
constexpr Side Opponent(Side side) { return side == Side::kWhite ? Side::kBlack : Side::kWhite; }

}  // namespace chess_cpp

#endif  // DOMAINS_GAMES_LIBS_CHESS_CPP_SIDE_H
