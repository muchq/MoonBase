#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_BOARD_SCAN_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_BOARD_SCAN_H

#include <array>
#include <optional>
#include <vector>

#include "chess.hpp"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/chess_cpp/side.h"

namespace one_d4 {

// Square-by-square and along-ray walks, in a fixed order.
//
// Rank 8 down to rank 1, file a to h — the order the Java pipeline's 8x8
// walk produced. Two things depend on it: the order a ply's rows reach the
// API (pinned by detectors_test, not by the goldens, which are sorted), and
// which piece gets named when several qualify — FirstInScanOrder picks the
// same checker Java's scan did, and that the goldens do pin.
//
// Here rather than in chess_cpp::facts because a ray walk carries a
// detector's question ("what is behind the first piece"); facts answers
// position-shaped ones.

/// Scan coordinates: `dr` +1 is toward rank 1, `dc` +1 is toward file h.
struct Direction {
  int dr = 0;
  int dc = 0;

  constexpr bool IsDiagonal() const { return dr != 0 && dc != 0; }
  constexpr bool IsStraight() const { return dr == 0 || dc == 0; }
  constexpr Direction Reversed() const { return Direction{-dr, -dc}; }
  constexpr bool operator==(const Direction& other) const {
    return dr == other.dr && dc == other.dc;
  }
};

/// The eight queen directions, in visit order.
inline constexpr std::array<Direction, 8> kDirections = {{
    {0, 1},
    {0, -1},
    {1, 0},
    {-1, 0},
    {1, 1},
    {1, -1},
    {-1, 1},
    {-1, -1},
}};

/// The square at scan coordinates: row 0 is rank 8, row 7 is rank 1; col 0
/// is file a.
constexpr chess::Square SquareAt(int row, int col) { return chess::Square((7 - row) * 8 + col); }

constexpr int RowOf(chess::Square square) { return 7 - square.index() / 8; }
constexpr int ColOf(chess::Square square) { return square.index() % 8; }

/// The square one step from (row, col), or nullopt off the edge.
constexpr std::optional<chess::Square> Step(int row, int col, Direction direction) {
  const int r = row + direction.dr;
  const int c = col + direction.dc;
  if (r < 0 || r > 7 || c < 0 || c > 7) return std::nullopt;
  return SquareAt(r, c);
}

/// Calls `visit(row, col)` for all 64 squares in scan order.
template <typename Visitor>
void ForEachSquare(Visitor&& visit) {
  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
      visit(row, col);
    }
  }
}

/// pawn=1 … king=6. An ordering, not an evaluation — the king ranking
/// highest is what makes a skewer's "more valuable in front" come out.
constexpr int Value(chess::PieceType type) {
  return type == chess::PieceType::NONE ? 0 : static_cast<int>(type) + 1;
}

int Value(chess::Piece piece);

/// Queen anywhere, bishop diagonally, rook straight.
bool SlidesAlong(chess::Piece piece, Direction direction);

/// False for an empty square — the reason this exists, since
/// `piece.color() == ToColor(side)` reads the same and answers differently.
bool BelongsTo(chess::Piece piece, chess_cpp::Side side);

/// What the cheapest piece of `by` attacking `square` is worth, or nullopt
/// when nothing of theirs attacks it. The piece that would actually take.
std::optional<int> CheapestAttackerValue(const chess::Board& board, chess_cpp::Side by,
                                         chess::Square square);

/// The first set square in scan order.
std::optional<chess::Square> FirstInScanOrder(chess::Bitboard squares);

/// First occupied square along `direction`, exclusive of the start.
std::optional<chess::Square> FirstPieceAlong(const chess::Board& board, int row, int col,
                                             Direction direction);

// Reading the move that produced a position. Taken from the board and the
// move, never from the SAN text: '#' and '+' are things a PGN writer
// chooses to include.

/// Where the move put a piece. Two squares for castling — the king's and
/// the rook's — because the rook lands on a new line and can pin, skewer
/// and attack from it, which is the half the notation "O-O" hides.
std::vector<chess::Square> LandedOn(const chess_cpp::Position& position);

/// The rook's destination in a castling move, given the king's origin.
chess::Square CastledRookTo(chess::Square king_from, bool kingside);

bool IsPromotion(const chess_cpp::Position& position);

/// The move just played mated.
bool IsCheckmate(const chess_cpp::Position& position);

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_BOARD_SCAN_H
