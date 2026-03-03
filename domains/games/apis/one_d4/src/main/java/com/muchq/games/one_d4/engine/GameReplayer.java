package com.muchq.games.one_d4.engine;

import chariot.chess.Board;
import chariot.model.PGN;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class GameReplayer {

  public List<PositionContext> replay(String moveText) {
    List<PositionContext> positions = new ArrayList<>();
    Board board = Board.ofStandard();

    positions.add(new PositionContext(0, board.toFEN(), true, null));

    List<String> moves = extractMoves(moveText);
    int moveNumber = 1;
    boolean whiteToMove = true;

    for (String move : moves) {
      board = board.play(move);
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
