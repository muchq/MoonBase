package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects PROMOTION_WITH_CHECK: a pawn promotes and the promoted piece itself delivers check.
 *
 * <p>The previous notation-only heuristic ({@code contains("=") && endsWith("+")}) also fired for
 * discovered checks (e.g. a rook behind the pawn gives check after the pawn leaves). This
 * implementation verifies via board analysis that the promoted piece at its destination square
 * actually attacks the enemy king.
 */
public class PromotionWithCheckDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.PROMOTION_WITH_CHECK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move == null || !move.contains("=") || !move.endsWith("+")) continue;

      if (promotedPieceDeliversCheck(ctx)) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(
                ctx, "Promotion with check at move " + ctx.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /**
   * Returns true iff the promoted piece at its destination square directly attacks the enemy king
   * in the position encoded by {@code ctx}.
   */
  static boolean promotedPieceDeliversCheck(PositionContext ctx) {
    String move = ctx.lastMove();
    int[] dest = BoardUtils.parsePromotionDestination(move);
    if (dest[0] == -1) return false;

    String placement = ctx.fen().split(" ")[0];
    int[][] board = PinDetector.parsePlacement(placement);

    // The side that just moved is the opposite of whose turn it now is
    boolean moverIsWhite = !ctx.whiteToMove();

    // The promoted piece occupies the destination in the after-position
    int promotedPiece = board[dest[0]][dest[1]];
    if (promotedPiece == 0 || (promotedPiece > 0) != moverIsWhite) return false;

    // Find the enemy king
    int[] kingPos = BoardUtils.findKing(board, !moverIsWhite);
    if (kingPos[0] == -1) return false;

    return BoardUtils.pieceAttacks(board, dest[0], dest[1], kingPos[0], kingPos[1]);
  }
}
