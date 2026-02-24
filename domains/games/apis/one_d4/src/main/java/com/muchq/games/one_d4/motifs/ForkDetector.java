package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class ForkDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.FORK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // Check if any piece attacks two or more enemy pieces of significant value
      if (hasFork(board, !ctx.whiteToMove())) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(ctx, "Fork detected at move " + ctx.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  private boolean hasFork(int[][] board, boolean attackerIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        boolean isWhite = piece > 0;
        if (isWhite != attackerIsWhite) continue;

        int absPiece = Math.abs(piece);
        List<int[]> attacked = getAttackedSquares(board, r, c, absPiece, attackerIsWhite);

        // Count how many valuable enemy pieces are attacked
        int valuableTargets = 0;
        for (int[] sq : attacked) {
          int target = board[sq[0]][sq[1]];
          if (target != 0 && (target > 0) != attackerIsWhite) {
            int targetValue = Math.abs(target);
            // Target must be at least a knight/bishop (value >= 2)
            if (targetValue >= 2) {
              valuableTargets++;
            }
          }
        }
        if (valuableTargets >= 2) return true;
      }
    }
    return false;
  }

  private List<int[]> getAttackedSquares(
      int[][] board, int r, int c, int pieceType, boolean isWhite) {
    List<int[]> squares = new ArrayList<>();
    switch (pieceType) {
      case 2 -> addKnightAttacks(r, c, squares); // Knight
      case 1 -> addPawnAttacks(r, c, isWhite, squares); // Pawn
      case 3 ->
          addSlidingAttacks(
              board, r, c, new int[][] {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}, squares); // Bishop
      case 4 ->
          addSlidingAttacks(
              board, r, c, new int[][] {{0, 1}, {0, -1}, {1, 0}, {-1, 0}}, squares); // Rook
      case 5 -> { // Queen
        addSlidingAttacks(
            board,
            r,
            c,
            new int[][] {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}},
            squares);
      }
      case 6 -> addKingAttacks(r, c, squares); // King
    }
    return squares;
  }

  private void addKnightAttacks(int r, int c, List<int[]> squares) {
    int[][] offsets = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    for (int[] off : offsets) {
      int nr = r + off[0], nc = c + off[1];
      if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
        squares.add(new int[] {nr, nc});
      }
    }
  }

  private void addPawnAttacks(int r, int c, boolean isWhite, List<int[]> squares) {
    int dir = isWhite ? -1 : 1;
    if (c > 0 && r + dir >= 0 && r + dir < 8) squares.add(new int[] {r + dir, c - 1});
    if (c < 7 && r + dir >= 0 && r + dir < 8) squares.add(new int[] {r + dir, c + 1});
  }

  private void addKingAttacks(int r, int c, List<int[]> squares) {
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        if (dr == 0 && dc == 0) continue;
        int nr = r + dr, nc = c + dc;
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
          squares.add(new int[] {nr, nc});
        }
      }
    }
  }

  private void addSlidingAttacks(
      int[][] board, int r, int c, int[][] directions, List<int[]> squares) {
    for (int[] dir : directions) {
      int nr = r + dir[0], nc = c + dir[1];
      while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
        squares.add(new int[] {nr, nc});
        if (board[nr][nc] != 0) break; // blocked
        nr += dir[0];
        nc += dir[1];
      }
    }
  }
}
