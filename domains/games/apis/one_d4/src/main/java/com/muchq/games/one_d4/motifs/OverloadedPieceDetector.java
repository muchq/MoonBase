package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects OVERLOADED_PIECE: a single defending piece that defends two or more friendly pieces that
 * are each under attack by the opponent. If that defender is used to defend one target, the other
 * target falls.
 *
 * <p>Detection (per position): for each piece of the side that just moved, find the opposing
 * (defending) side's pieces that are under attack. Then check if any single defending piece is the
 * sole or shared defender of two or more such attacked squares.
 */
public class OverloadedPieceDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.OVERLOADED_PIECE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      if (ctx.lastMove() == null) continue;

      String placement = ctx.fen().split(" ")[0];
      int[][] board = PinDetector.parsePlacement(placement);

      // After the move, the attacker is !ctx.whiteToMove() and the defender is ctx.whiteToMove()
      boolean attackerIsWhite = !ctx.whiteToMove();
      boolean defenderIsWhite = ctx.whiteToMove();

      int[] overloaded = findOverloadedPiece(board, attackerIsWhite, defenderIsWhite);
      if (overloaded != null) {
        int r = overloaded[0], c = overloaded[1];
        String target = BoardUtils.pieceNotation(board[r][c], r, c);
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.withPiece(
                ctx, "Overloaded piece at move " + ctx.moveNumber(), null, target);
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /**
   * Returns {r, c} of the first overloaded defending piece found, or null if none. An overloaded
   * piece defends 2+ squares that are each under attack.
   */
  private int[] findOverloadedPiece(
      int[][] board, boolean attackerIsWhite, boolean defenderIsWhite) {
    // Collect squares with defending-side pieces that are under attack by the attacking side
    List<int[]> attackedDefendingSquares = new ArrayList<>();
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        if ((piece > 0) != defenderIsWhite) continue;
        if (BoardUtils.countAttackers(board, r, c, attackerIsWhite) > 0) {
          attackedDefendingSquares.add(new int[] {r, c});
        }
      }
    }

    if (attackedDefendingSquares.size() < 2) return null;

    // For each defending piece, count how many attacked defending squares it can recapture on
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        if ((piece > 0) != defenderIsWhite) continue;

        int defended = 0;
        for (int[] sq : attackedDefendingSquares) {
          if (sq[0] == r && sq[1] == c) continue; // piece can't defend itself this way
          if (BoardUtils.pieceAttacks(board, r, c, sq[0], sq[1])) {
            defended++;
          }
        }
        if (defended >= 2) return new int[] {r, c};
      }
    }
    return null;
  }
}
