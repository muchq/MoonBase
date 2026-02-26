package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects SMOTHERED_MATE: a knight delivers checkmate while the king is surrounded (smothered) by
 * its own pieces so it cannot escape.
 *
 * <p>Detection criteria:
 *
 * <ol>
 *   <li>The move ends with '#'.
 *   <li>A knight of the mating side attacks the checkmated king.
 *   <li>All squares adjacent to the checkmated king are either off-board or occupied by the king's
 *       own pieces (so the king is "smothered" — it can't step anywhere to escape).
 * </ol>
 */
public class SmotheredMateDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.SMOTHERED_MATE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move == null || !move.endsWith("#")) continue;

      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      boolean loserIsWhite = ctx.whiteToMove();
      int[] kingPos = BoardUtils.findKing(board, loserIsWhite);
      if (kingPos[0] == -1) continue;

      // Check that a knight of the mating side attacks the king
      boolean matedByKnight = false;
      int knightPiece = loserIsWhite ? -2 : 2; // enemy knight
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          if (board[r][c] == knightPiece
              && BoardUtils.pieceAttacks(board, r, c, kingPos[0], kingPos[1])) {
            matedByKnight = true;
            break;
          }
        }
        if (matedByKnight) break;
      }
      if (!matedByKnight) continue;

      // Confirm king is smothered: all 8 adjacent squares are off-board or own pieces
      if (!isSmothered(board, kingPos[0], kingPos[1], loserIsWhite)) continue;

      GameFeatures.MotifOccurrence occ =
          GameFeatures.MotifOccurrence.from(ctx, "Smothered mate at move " + ctx.moveNumber());
      if (occ != null) occurrences.add(occ);
    }

    return occurrences;
  }

  private boolean isSmothered(int[][] board, int kr, int kc, boolean kingIsWhite) {
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        if (dr == 0 && dc == 0) continue;
        int nr = kr + dr, nc = kc + dc;
        if (nr < 0 || nr >= 8 || nc < 0 || nc >= 8) continue; // off-board counts as blocked
        int piece = board[nr][nc];
        // If an adjacent square is empty or occupied by an enemy piece, not fully smothered
        if (piece == 0 || (piece > 0) != kingIsWhite) return false;
      }
    }
    return true;
  }
}
