package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class SkewerDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.SKEWER;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // A skewer is the opposite of a pin: a more valuable piece is in front,
      // and when it moves, a less valuable piece behind is captured.
      List<int[]> skewers = findSkewers(board, !ctx.whiteToMove());
      for (int[] skewer : skewers) {
        // skewer = {attackerR, attackerC, frontR, frontC}
        int ar = skewer[0], ac = skewer[1];
        int fr = skewer[2], fc = skewer[3];
        String attacker = BoardUtils.pieceNotation(board[ar][ac], ar, ac);
        String target = BoardUtils.pieceNotation(board[fr][fc], fr, fc);
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.withPiece(
                ctx, "Skewer detected at move " + ctx.moveNumber(), attacker, target);
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /** Returns a list of {attackerR, attackerC, frontPieceR, frontPieceC} for each skewer found. */
  private List<int[]> findSkewers(int[][] board, boolean attackerIsWhite) {
    int[][] directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    List<int[]> result = new ArrayList<>();

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        boolean isWhite = piece > 0;
        if (isWhite != attackerIsWhite) continue;

        int absPiece = Math.abs(piece);
        // Only sliding pieces can skewer
        if (absPiece != 3 && absPiece != 4 && absPiece != 5) continue;

        for (int[] dir : directions) {
          if (!canAttackDirection(absPiece, dir)) continue;
          int[] skewer = findSkewerAlongRay(board, r, c, dir[0], dir[1], attackerIsWhite);
          if (skewer != null) {
            result.add(new int[] {r, c, skewer[0], skewer[1]});
          }
        }
      }
    }
    return result;
  }

  private boolean canAttackDirection(int absPiece, int[] dir) {
    boolean isDiagonal = dir[0] != 0 && dir[1] != 0;
    boolean isStraight = dir[0] == 0 || dir[1] == 0;
    if (absPiece == 5) return true; // Queen
    if (absPiece == 3) return isDiagonal; // Bishop
    if (absPiece == 4) return isStraight; // Rook
    return false;
  }

  /**
   * Returns {frontPieceR, frontPieceC} if there's a skewer along the ray, or null. A skewer exists
   * when the front piece is more valuable than the piece behind it.
   */
  private int[] findSkewerAlongRay(
      int[][] board, int ar, int ac, int dr, int dc, boolean attackerIsWhite) {
    int r = ar + dr, c = ac + dc;
    int firstValue = -1;
    int firstR = -1, firstC = -1;

    while (r >= 0 && r < 8 && c >= 0 && c < 8) {
      int piece = board[r][c];
      if (piece != 0) {
        boolean isWhite = piece > 0;
        if (isWhite == attackerIsWhite) return null; // friendly piece blocks

        int value = Math.abs(piece);
        if (firstValue == -1) {
          firstValue = value;
          firstR = r;
          firstC = c;
        } else {
          // Skewer: first piece (in front) is more valuable than second
          if (firstValue > value && value >= 2) {
            return new int[] {firstR, firstC};
          }
          return null;
        }
      }
      r += dr;
      c += dc;
    }
    return null;
  }
}
