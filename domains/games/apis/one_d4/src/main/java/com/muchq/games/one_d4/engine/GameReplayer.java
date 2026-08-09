package com.muchq.games.one_d4.engine;

import chariot.model.PGN;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class GameReplayer {

  /**
   * The caller gave up and the thread was interrupted, so replay stopped early.
   *
   * <p>Not an IllegalArgumentException: the PGN may be perfectly legal, and reporting this as bad
   * input would tell a caller to fix something that was never wrong. Callers that time out map it
   * to their own timeout answer; indexing never interrupts, so it never sees this.
   */
  public static class AnalysisCancelledException extends RuntimeException {
    public AnalysisCancelledException(String message) {
      super(message);
    }
  }

  public List<PositionContext> replay(String moveText) {
    List<PositionContext> positions = new ArrayList<>();
    ReplayBoard board = ReplayBoard.standard();

    positions.add(new PositionContext(0, board.toFEN(), true, null));

    List<String> moves = extractMoves(moveText);
    int moveNumber = 1;
    boolean whiteToMove = true;

    for (String move : moves) {
      // One check per move, not per inner board operation: enough to end an abandoned analysis
      // promptly on a 40k-move PGN, cheap enough to be invisible next to playing the move.
      if (Thread.currentThread().isInterrupted()) {
        throw new AnalysisCancelledException("replay cancelled at move " + moveNumber);
      }
      try {
        board.play(move);
      } catch (RuntimeException e) {
        // The board's own message has no game context; production logs need the ply and position
        // to debug a bad PGN.
        throw new IllegalArgumentException(
            "Failed at move "
                + moveNumber
                + (whiteToMove ? ". " : "... ")
                + move
                + " from "
                + positions.get(positions.size() - 1).fen(),
            e);
      }
      whiteToMove = !whiteToMove;
      positions.add(new PositionContext(moveNumber, board.toFEN(), whiteToMove, move));
      if (whiteToMove) {
        // Black just played — advance move number for the next pair.
        moveNumber++;
      }
    }

    return positions;
  }

  private List<String> extractMoves(String moveText) {
    return PGN.Text.parse(moveText)
        .filter(t -> t instanceof PGN.Text.Move)
        .map(t -> ((PGN.Text.Move) t).san())
        .filter(GameReplayer::isValidSan)
        .toList();
  }

  /**
   * Returns true if {@code san} looks like a playable SAN move. Chariot has no dedicated token type
   * for NAG annotations (e.g. {@code $1}), so they can leak through as Move tokens with invalid SAN
   * strings. We reject them here before trying to play them on the board.
   */
  private static boolean isValidSan(String san) {
    if (san.isEmpty()) return false;
    char c = san.charAt(0);
    return (c >= 'a' && c <= 'h') // pawn move or file-disambiguation
        || c == 'K'
        || c == 'Q'
        || c == 'R'
        || c == 'B'
        || c == 'N' // piece move
        || san.startsWith("O-O"); // castling
  }
}
