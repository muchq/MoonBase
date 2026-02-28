package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class CheckDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.CHECK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move != null && (move.endsWith("+") || move.endsWith("#"))) {
        boolean moverIsWhite = !ctx.whiteToMove();
        String placement = ctx.fen().split(" ")[0];
        int[][] board = PinDetector.parsePlacement(placement);

        int[] checker = BoardUtils.findCheckingPiece(board, moverIsWhite);
        int[] king = BoardUtils.findKing(board, !moverIsWhite);

        String attacker =
            checker != null
                ? BoardUtils.pieceNotation(board[checker[0]][checker[1]], checker[0], checker[1])
                : null;
        String target =
            king[0] != -1
                ? BoardUtils.pieceNotation(board[king[0]][king[1]], king[0], king[1])
                : null;

        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.withPiece(
                ctx, "Check at move " + ctx.moveNumber(), attacker, target);
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }
}
