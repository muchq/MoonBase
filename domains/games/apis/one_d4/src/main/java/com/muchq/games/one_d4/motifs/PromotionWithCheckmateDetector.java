package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects PROMOTION_WITH_CHECKMATE: a pawn promotes and the promoted piece itself delivers
 * checkmate. Like {@link PromotionWithCheckDetector}, uses board analysis to confirm the promoted
 * piece (not a hidden sliding piece) is the one delivering the mating check.
 */
public class PromotionWithCheckmateDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.PROMOTION_WITH_CHECKMATE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move == null || !move.contains("=") || !move.endsWith("#")) continue;

      if (PromotionWithCheckDetector.promotedPieceDeliversCheck(ctx)) {
        String placement = ctx.fen().split(" ")[0];
        int[][] board = PinDetector.parsePlacement(placement);
        boolean moverIsWhite = !ctx.whiteToMove();
        int[] dest = BoardUtils.parsePromotionDestination(move);
        int[] kingPos = BoardUtils.findKing(board, !moverIsWhite);

        String attacker =
            dest[0] != -1
                ? BoardUtils.pieceNotation(board[dest[0]][dest[1]], dest[0], dest[1])
                : null;
        String target =
            kingPos[0] != -1
                ? BoardUtils.pieceNotation(board[kingPos[0]][kingPos[1]], kingPos[0], kingPos[1])
                : null;

        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.withMate(
                ctx, "Promotion with checkmate at move " + ctx.moveNumber(), attacker, target);
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }
}
