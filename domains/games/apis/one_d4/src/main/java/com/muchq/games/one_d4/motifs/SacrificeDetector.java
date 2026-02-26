package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects SACRIFICE: a move where a higher-value piece captures a lower-value piece (the capturer
 * gives up more material than it gains).
 *
 * <p>Detection: compare consecutive position pairs. Find the capture square (a square where the
 * piece changed from an enemy piece to a friendly piece). If the capturing piece's material value
 * strictly exceeds the captured piece's value, it is a sacrifice.
 *
 * <p>Piece values: P=1, N=2, B=3, R=4, Q=5, K=6.
 */
public class SacrificeDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.SACRIFICE;
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

      if (hasSacrifice(boardBefore, boardAfter, moverIsWhite)) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(after, "Sacrifice at move " + after.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  private boolean hasSacrifice(int[][] before, int[][] after, boolean moverIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pieceBefore = before[r][c];
        int pieceAfter = after[r][c];

        // A capture: square had an enemy piece and now has a mover's piece
        if (pieceBefore == 0 || pieceAfter == 0) continue;
        boolean beforeIsEnemy = (pieceBefore > 0) != moverIsWhite;
        boolean afterIsMover = (pieceAfter > 0) == moverIsWhite;
        if (!beforeIsEnemy || !afterIsMover) continue;

        int capturedValue = Math.abs(pieceBefore);
        int capturerValue = Math.abs(pieceAfter);

        // Sacrifice: the capturing piece is worth more than what it captured
        if (capturerValue > capturedValue) {
          return true;
        }
      }
    }
    return false;
  }
}
