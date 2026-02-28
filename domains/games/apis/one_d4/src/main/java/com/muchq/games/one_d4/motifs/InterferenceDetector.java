package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects INTERFERENCE: a piece is placed on a square that blocks an enemy sliding piece's
 * previously open line of attack or defense.
 *
 * <p>Detection: for each move, find the destination square (a previously empty square that now has
 * a friendly piece). Then check whether any enemy sliding piece (queen, rook, or bishop) had a
 * clear attack line through that square in the before-position. By occupying the square, the
 * friendly piece has interfered with the enemy piece's line.
 */
public class InterferenceDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.INTERFERENCE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (int i = 1; i < positions.size(); i++) {
      PositionContext before = positions.get(i - 1);
      PositionContext after = positions.get(i);

      String beforePlacement = before.fen().split(" ")[0];
      String afterPlacement = after.fen().split(" ")[0];
      int[][] boardBefore = PinDetector.parsePlacement(beforePlacement);
      int[][] boardAfter = PinDetector.parsePlacement(afterPlacement);

      boolean moverIsWhite = !after.whiteToMove();

      int[] result = findInterference(boardBefore, boardAfter, moverIsWhite);
      if (result != null) {
        // result = {destR, destC, enemyR, enemyC}
        int destR = result[0], destC = result[1];
        int enemyR = result[2], enemyC = result[3];
        // attacker = the interfering piece (mover's piece at destination)
        String attacker = BoardUtils.pieceNotation(boardAfter[destR][destC], destR, destC);
        // target = the blocked enemy sliding piece
        String target = BoardUtils.pieceNotation(boardBefore[enemyR][enemyC], enemyR, enemyC);

        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.withPiece(
                after, "Interference at move " + after.moveNumber(), attacker, target);
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /** Returns {destR, destC, enemyR, enemyC} for the first interference found, or null if none. */
  private int[] findInterference(int[][] before, int[][] after, boolean moverIsWhite) {
    // Find the destination square: was empty before, has a mover's piece after
    for (int dr = 0; dr < 8; dr++) {
      for (int dc = 0; dc < 8; dc++) {
        if (before[dr][dc] != 0) continue; // must have been empty
        int pieceAfter = after[dr][dc];
        if (pieceAfter == 0) continue;
        if ((pieceAfter > 0) != moverIsWhite) continue; // must be mover's piece

        // Found the destination square (dr, dc). Check if any enemy sliding piece
        // had a clear attack line THROUGH this square in the before-position.
        int[] enemyPos = findBlockedEnemySlidingPiece(before, dr, dc, moverIsWhite);
        if (enemyPos != null) {
          return new int[] {dr, dc, enemyPos[0], enemyPos[1]};
        }
      }
    }
    return null;
  }

  /**
   * Returns {r, c} of the first enemy sliding piece whose line was blocked by (destR, destC), or
   * null if none.
   */
  private int[] findBlockedEnemySlidingPiece(
      int[][] before, int destR, int destC, boolean moverIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = before[r][c];
        if (piece == 0) continue;
        if ((piece > 0) == moverIsWhite) continue; // must be enemy piece
        int absPiece = Math.abs(piece);
        if (absPiece != 3 && absPiece != 4 && absPiece != 5) continue; // sliding pieces only

        // Check if this sliding piece attacked through (destR, destC) in before-position.
        if (BoardUtils.pieceAttacks(before, r, c, destR, destC)
            && lineExtendsThrough(before, r, c, destR, destC)) {
          return new int[] {r, c};
        }
      }
    }
    return null;
  }

  /**
   * Returns true if the sliding piece at (pr, pc) attacks THROUGH (destR, destC) — i.e. the line
   * from (pr,pc) through (destR,destC) extends further on the board.
   */
  private boolean lineExtendsThrough(int[][] board, int pr, int pc, int destR, int destC) {
    int dr = Integer.signum(destR - pr);
    int dc = Integer.signum(destC - pc);
    int nr = destR + dr, nc = destC + dc;
    return nr >= 0 && nr < 8 && nc >= 0 && nc < 8;
  }
}
