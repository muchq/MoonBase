package com.muchq.games.one_d4.motifs;

/**
 * Shared board analysis utilities for motif detectors. All coordinates use the board array
 * convention where board[0][0] = a8 (rank 8, file a) and board[7][7] = h1 (rank 1, file h). Piece
 * values: P=1, N=2, B=3, R=4, Q=5, K=6; negative for black pieces.
 */
class BoardUtils {

  private BoardUtils() {}

  /**
   * Returns true if the piece at (pieceRow, pieceCol) attacks the square (targetRow, targetCol).
   * Handles all piece types including path-clearing for sliding pieces.
   */
  static boolean pieceAttacks(
      int[][] board, int pieceRow, int pieceCol, int targetRow, int targetCol) {
    int piece = board[pieceRow][pieceCol];
    if (piece == 0) return false;
    int pieceType = Math.abs(piece);
    boolean pieceIsWhite = piece > 0;

    int rowDelta = targetRow - pieceRow;
    int colDelta = targetCol - pieceCol;

    switch (pieceType) {
      case 1: // Pawn — attacks diagonally one step in the forward direction
        int pawnDir = pieceIsWhite ? -1 : 1;
        return rowDelta == pawnDir && Math.abs(colDelta) == 1;

      case 2: // Knight — L-shape
        int absRowDelta = Math.abs(rowDelta), absColDelta = Math.abs(colDelta);
        return (absRowDelta == 2 && absColDelta == 1) || (absRowDelta == 1 && absColDelta == 2);

      case 3: // Bishop — diagonal only
        if (Math.abs(rowDelta) != Math.abs(colDelta) || rowDelta == 0) return false;
        return isPathClear(board, pieceRow, pieceCol, targetRow, targetCol);

      case 4: // Rook — straight lines only
        if (rowDelta != 0 && colDelta != 0) return false;
        return isPathClear(board, pieceRow, pieceCol, targetRow, targetCol);

      case 5: // Queen — any straight or diagonal
        if (rowDelta != 0 && colDelta != 0 && Math.abs(rowDelta) != Math.abs(colDelta))
          return false;
        return isPathClear(board, pieceRow, pieceCol, targetRow, targetCol);

      case 6: // King — one step in any direction
        return Math.abs(rowDelta) <= 1
            && Math.abs(colDelta) <= 1
            && (rowDelta != 0 || colDelta != 0);

      default:
        return false;
    }
  }

  /** Returns true if all squares strictly between (fromRow,fromCol) and (toRow,toCol) are empty. */
  static boolean isPathClear(int[][] board, int fromRow, int fromCol, int toRow, int toCol) {
    int rowStep = Integer.signum(toRow - fromRow);
    int colStep = Integer.signum(toCol - fromCol);
    int row = fromRow + rowStep, col = fromCol + colStep;
    while (row != toRow || col != toCol) {
      if (board[row][col] != 0) return false;
      row += rowStep;
      col += colStep;
    }
    return true;
  }

  /**
   * Counts how many pieces of {@code attackerIsWhite} color attack the square (targetRow,
   * targetCol). Ignores any piece that might be standing on (targetRow, targetCol) itself.
   */
  static int countAttackers(int[][] board, int targetRow, int targetCol, boolean attackerIsWhite) {
    int count = 0;
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        if (row == targetRow && col == targetCol) continue;
        int piece = board[row][col];
        if (piece == 0) continue;
        if ((piece > 0) != attackerIsWhite) continue;
        if (pieceAttacks(board, row, col, targetRow, targetCol)) {
          count++;
        }
      }
    }
    return count;
  }

  /**
   * Finds the row of the king of the given color. Returns -1 if not found. Stores the result in a
   * two-element array {row, col}.
   */
  static int[] findKing(int[][] board, boolean kingIsWhite) {
    int kingPiece = kingIsWhite ? 6 : -6;
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        if (board[r][c] == kingPiece) {
          return new int[] {r, c};
        }
      }
    }
    return new int[] {-1, -1};
  }

  /**
   * Converts board coordinates to algebraic square name. (row=7, col=4) → "e1"; (row=0, col=0) →
   * "a8".
   */
  static String squareName(int row, int col) {
    char file = (char) ('a' + col);
    char rank = (char) ('8' - row);
    return "" + file + rank;
  }

  /**
   * Returns the piece-letter notation for a piece at a given square. White pieces use uppercase,
   * black pieces lowercase. Example: pieceNotation(5, 7, 4) → "Qe1"; pieceNotation(-6, 0, 4) →
   * "ke8".
   */
  static String pieceNotation(int piece, int row, int col) {
    boolean white = piece > 0;
    int abs = Math.abs(piece);
    char letter =
        switch (abs) {
          case 1 -> 'P';
          case 2 -> 'N';
          case 3 -> 'B';
          case 4 -> 'R';
          case 5 -> 'Q';
          case 6 -> 'K';
          default -> '?';
        };
    char l = white ? letter : Character.toLowerCase(letter);
    return l + squareName(row, col);
  }

  /**
   * Scans all of the mover's pieces and returns {row, col} of the first one attacking the enemy
   * king, or null if none found.
   */
  static int[] findCheckingPiece(int[][] board, boolean moverIsWhite) {
    int[] kingPos = findKing(board, !moverIsWhite);
    if (kingPos[0] == -1) return null;
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) continue;
        if ((piece > 0) != moverIsWhite) continue;
        if (pieceAttacks(board, r, c, kingPos[0], kingPos[1])) {
          return new int[] {r, c};
        }
      }
    }
    return null;
  }

  /**
   * Parses the destination square from a promotion move like "e8=Q+" or "axb8=N#". Returns {row,
   * col} in board-array coordinates, or {-1,-1} on parse failure.
   */
  static int[] parsePromotionDestination(String move) {
    int eqIdx = move.indexOf('=');
    if (eqIdx < 2) return new int[] {-1, -1};
    // The two characters before '=' are the destination square, e.g. "e8" or "b8"
    String dest = move.substring(eqIdx - 2, eqIdx);
    if (dest.length() != 2) return new int[] {-1, -1};
    char fileChar = dest.charAt(0);
    char rankChar = dest.charAt(1);
    if (fileChar < 'a' || fileChar > 'h' || rankChar < '1' || rankChar > '8') {
      return new int[] {-1, -1};
    }
    int col = fileChar - 'a'; // 0-7
    int row = 8 - (rankChar - '0'); // rank 8 → row 0, rank 1 → row 7
    return new int[] {row, col};
  }
}
