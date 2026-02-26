package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects DOUBLE_CHECK: two pieces simultaneously give check after a single move. This occurs when
 * a piece moves and simultaneously the moving piece delivers check AND a second piece (previously
 * blocked) also delivers check.
 *
 * <p>Detection: after a move ending with '+', count the number of attacker pieces that reach the
 * enemy king. If two or more, it is a double check.
 */
public class DoubleCheckDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.DOUBLE_CHECK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      // Double check always delivers check; '#' is also a double check if two pieces give mate
      if (move == null || (!move.endsWith("+") && !move.endsWith("#"))) continue;

      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // The side now to move is in check; find their king
      boolean kingIsWhite = ctx.whiteToMove();
      int[] kingPos = BoardUtils.findKing(board, kingIsWhite);
      if (kingPos[0] == -1) continue;

      // Count how many enemy pieces attack the king
      int checkers = BoardUtils.countAttackers(board, kingPos[0], kingPos[1], !kingIsWhite);
      if (checkers >= 2) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(ctx, "Double check at move " + ctx.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }
}
