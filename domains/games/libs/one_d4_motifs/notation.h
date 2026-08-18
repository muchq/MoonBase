#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_NOTATION_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_NOTATION_H

#include <optional>
#include <string>

#include "chess.hpp"

namespace one_d4 {

// How a stored occurrence names a piece: a piece *on a square*, which
// neither SAN nor UCI spells. Clients and ChessQL see these strings.

/// 'P' 'N' 'B' 'R' 'Q' 'K' for White, lowercase for Black, '?' for an empty
/// square.
char PieceLetter(chess::Piece piece);

/// "e4".
std::string SquareName(chess::Square square);

/// Letter + square: "Qh5" for a white queen on h5, "ke8" for a black king.
std::string PieceNotation(chess::Piece piece, chess::Square square);

/// Letter + origin + destination: "Ne2g3". A promotion has no destination
/// in this spelling — the pawn that left e7 is not the queen that arrived —
/// and renders as "??", which is what existing rows carry.
std::string MovedPieceNotation(chess::Piece piece, chess::Square from,
                               std::optional<chess::Square> to);

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_NOTATION_H
