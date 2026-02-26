package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Heuristic detector for ZUGZWANG: a position where the side to move would prefer to pass, because
 * any legal move worsens their position.
 *
 * <p>True zugzwang detection requires engine evaluation of all candidate moves. This implementation
 * uses a positional heuristic: a position is flagged as potential zugzwang when it is an endgame
 * (≤ 8 total pieces, no queens) AND the side to move has very limited mobility:
 *
 * <ul>
 *   <li>All pawns are blocked (the square directly in front is occupied by any piece).
 *   <li>No non-pawn, non-king piece can move to any empty square in one step.
 * </ul>
 *
 * This captures the most common zugzwang patterns (king-and-pawn endgames, rook endgames with
 * blocked structure) while producing few false positives.
 */
public class ZugzwangDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.ZUGZWANG;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      if (ctx.lastMove() == null) continue; // skip initial position

      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // Only flag endgame positions (few pieces, no queens)
      if (!isEndgame(board)) continue;

      boolean toMove = ctx.whiteToMove();
      if (isLikelyZugzwang(board, toMove)) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(
                ctx, "Zugzwang (heuristic) at move " + ctx.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  private boolean isEndgame(int[][] board) {
    int total = 0;
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        total++;
        if (Math.abs(piece) == 5) return false; // queen present → not a simple endgame
      }
    }
    return total <= 8;
  }

  /**
   * Returns true if the side to move ({@code toMove}) has very limited mobility: all pawns
   * blocked, and no non-king piece can step to an empty square.
   */
  private boolean isLikelyZugzwang(int[][] board, boolean toMove) {
    int pawnDir = toMove ? -1 : 1; // white pawns advance toward row 0, black toward row 7

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        if ((piece > 0) != toMove) continue; // only the side to move

        int absPiece = Math.abs(piece);
        if (absPiece == 6) continue; // skip king

        if (absPiece == 1) {
          // Pawn: check if the square in front is empty (pawn can advance)
          int nr = r + pawnDir;
          if (nr >= 0 && nr < 8 && board[nr][c] == 0) return false; // pawn can move
        } else {
          // Non-pawn piece: check if it can reach any empty square
          if (canReachEmptySquare(board, r, c, absPiece, toMove)) return false;
        }
      }
    }
    return true;
  }

  private boolean canReachEmptySquare(int[][] board, int r, int c, int absPiece, boolean isWhite) {
    switch (absPiece) {
      case 2 -> { // Knight
        int[][] offsets = {
          {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}
        };
        for (int[] off : offsets) {
          int nr = r + off[0], nc = c + off[1];
          if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && board[nr][nc] == 0) return true;
        }
      }
      case 3 -> { // Bishop
        int[][] dirs = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        if (canSlideToEmpty(board, r, c, dirs)) return true;
      }
      case 4 -> { // Rook
        int[][] dirs = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        if (canSlideToEmpty(board, r, c, dirs)) return true;
      }
      default -> {} // Q, K handled elsewhere or not applicable
    }
    return false;
  }

  private boolean canSlideToEmpty(int[][] board, int r, int c, int[][] dirs) {
    for (int[] dir : dirs) {
      int nr = r + dir[0], nc = c + dir[1];
      while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
        if (board[nr][nc] == 0) return true;
        break; // blocked by any piece
      }
    }
    return false;
  }
}
