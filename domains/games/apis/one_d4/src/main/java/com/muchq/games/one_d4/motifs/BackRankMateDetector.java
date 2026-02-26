package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects BACK_RANK_MATE: checkmate where the king is on its back rank (rank 1 for white, rank 8
 * for black), unable to escape because its own pieces block the forward escape squares.
 *
 * <p>Detection criteria:
 *
 * <ol>
 *   <li>The move ends with '#' (checkmate).
 *   <li>The checkmated king is on its back rank (row 7 for white king, row 0 for black king).
 *   <li>At least one of the escape squares on the adjacent rank is occupied by a friendly piece.
 * </ol>
 */
public class BackRankMateDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.BACK_RANK_MATE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move == null || !move.endsWith("#")) continue;

      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // The side that is checkmated is the side now to move (cannot escape)
      boolean loserIsWhite = ctx.whiteToMove();
      int backRankRow = loserIsWhite ? 7 : 0; // rank 1 for white (row 7), rank 8 for black (row 0)

      int[] kingPos = BoardUtils.findKing(board, loserIsWhite);
      if (kingPos[0] == -1 || kingPos[0] != backRankRow) continue;

      // Check that at least one adjacent-rank escape square is blocked by own piece
      int escapeRankRow = loserIsWhite ? 6 : 1; // rank 2 for white (row 6), rank 7 for black (row 1)
      boolean blockedByOwnPiece = false;
      for (int dc = -1; dc <= 1; dc++) {
        int ec = kingPos[1] + dc;
        if (ec < 0 || ec > 7) continue;
        int piece = board[escapeRankRow][ec];
        if (piece != 0 && (piece > 0) == loserIsWhite) {
          blockedByOwnPiece = true;
          break;
        }
      }
      if (!blockedByOwnPiece) continue;

      GameFeatures.MotifOccurrence occ =
          GameFeatures.MotifOccurrence.from(ctx, "Back rank mate at move " + ctx.moveNumber());
      if (occ != null) occurrences.add(occ);
    }

    return occurrences;
  }
}
