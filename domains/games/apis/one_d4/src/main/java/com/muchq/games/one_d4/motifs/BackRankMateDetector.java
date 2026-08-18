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
 *   <li>The checking piece is on that same back rank, so the mate is delivered along it. A queen or
 *       bishop giving mate from g7 is a support mate, not this one — ten of the thirteen rows in
 *       the parity corpus were exactly that.
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
    if (positions.isEmpty()) return occurrences;

    // Checkmate is always the last move of a game.
    PositionContext ctx = positions.get(positions.size() - 1);
    String move = ctx.lastMove();
    if (move == null || !move.endsWith("#")) return occurrences;

    String placement = ctx.fen().split(" ")[0];
    int[][] board = BoardUtils.parsePlacement(placement);

    // The side that is checkmated is the side now to move (cannot escape)
    boolean loserIsWhite = ctx.whiteToMove();
    int backRankRow = loserIsWhite ? 7 : 0; // rank 1 for white (row 7), rank 8 for black (row 0)

    int[] kingPos = BoardUtils.findKing(board, loserIsWhite);
    if (kingPos[0] == -1 || kingPos[0] != backRankRow) return occurrences;

    // Mated *along* the back rank. Without this the motif fires on any mate that lands on the
    // back rank with a friendly pawn nearby: a queen or bishop giving mate from g7 is neither a
    // back-rank mate nor rare, and ten of this corpus's thirteen rows were exactly that.
    // Any checker on that rank, not whichever the scan reaches first: on a double check the scan
    // runs rank 8 to rank 1, so a black king's back-rank checker is found first and a white king's
    // last, and the two mirror-image mates would classify differently.
    boolean moverIsWhite = !ctx.whiteToMove();
    int[] checker = null;
    for (int c = 0; c < 8; c++) {
      int piece = board[backRankRow][c];
      if (piece == 0 || (piece > 0) != moverIsWhite) continue;
      if (BoardUtils.pieceAttacks(board, backRankRow, c, kingPos[0], kingPos[1])) {
        checker = new int[] {backRankRow, c};
        break;
      }
    }
    if (checker == null) return occurrences;

    // ...and shut in by its own men rather than only by the attacker.
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
    if (!blockedByOwnPiece) return occurrences;

    String attacker =
        BoardUtils.pieceNotation(board[checker[0]][checker[1]], checker[0], checker[1]);
    String target = BoardUtils.pieceNotation(board[kingPos[0]][kingPos[1]], kingPos[0], kingPos[1]);

    GameFeatures.MotifOccurrence occ =
        GameFeatures.MotifOccurrence.withMate(
            ctx, "Back rank mate at move " + ctx.moveNumber(), attacker, target);
    if (occ != null) occurrences.add(occ);

    return occurrences;
  }
}
