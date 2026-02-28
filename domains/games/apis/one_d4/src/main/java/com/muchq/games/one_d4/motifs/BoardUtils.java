package com.muchq.games.one_d4.motifs;

/**
 * Shared board analysis utilities for motif detectors. All coordinates use the board array
 * convention where board[0][0] = a8 (rank 8, file a) and board[7][7] = h1 (rank 1, file h). Piece
 * values: P=1, N=2, B=3, R=4, Q=5, K=6; negative for black pieces.
 */
class BoardUtils {

  private BoardUtils() {}

  /**
   * Returns true if the piece at (pr, pc) attacks the square (tr, tc). Handles all piece types
   * including path-clearing for sliding pieces.
   */
  static boolean pieceAttacks(int[][] board, int pr, int pc, int tr, int tc) {
    int piece = board[pr][pc];
    if (piece == 0) return false;
    int absPiece = Math.abs(piece);
    boolean pieceIsWhite = piece > 0;

    int dr = tr - pr;
    int dc = tc - pc;

    switch (absPiece) {
      case 1: // Pawn — attacks diagonally one step in the forward direction
        int pawnDir = pieceIsWhite ? -1 : 1;
        return dr == pawnDir && Math.abs(dc) == 1;

      case 2: // Knight — L-shape
        int adr = Math.abs(dr), adc = Math.abs(dc);
        return (adr == 2 && adc == 1) || (adr == 1 && adc == 2);

      case 3: // Bishop — diagonal only
        if (Math.abs(dr) != Math.abs(dc) || dr == 0) return false;
        return isPathClear(board, pr, pc, tr, tc);

      case 4: // Rook — straight lines only
        if (dr != 0 && dc != 0) return false;
        return isPathClear(board, pr, pc, tr, tc);

      case 5: // Queen — any straight or diagonal
        if (dr != 0 && dc != 0 && Math.abs(dr) != Math.abs(dc)) return false;
        return isPathClear(board, pr, pc, tr, tc);

      case 6: // King — one step in any direction
        return Math.abs(dr) <= 1 && Math.abs(dc) <= 1 && (dr != 0 || dc != 0);

      default:
        return false;
    }
  }

  /** Returns true if all squares strictly between (fr,fc) and (tr,tc) are empty. */
  static boolean isPathClear(int[][] board, int fr, int fc, int tr, int tc) {
    int dr = Integer.signum(tr - fr);
    int dc = Integer.signum(tc - fc);
    int r = fr + dr, c = fc + dc;
    while (r != tr || c != tc) {
      if (board[r][c] != 0) return false;
      r += dr;
      c += dc;
    }
    return true;
  }

  /**
   * Counts how many pieces of {@code attackerIsWhite} color attack the square (tr, tc). Ignores any
   * piece that might be standing on (tr, tc) itself.
   */
  static int countAttackers(int[][] board, int tr, int tc, boolean attackerIsWhite) {
    int count = 0;
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        if (r == tr && c == tc) continue;
        int piece = board[r][c];
        if (piece == 0) continue;
        if ((piece > 0) != attackerIsWhite) continue;
        if (pieceAttacks(board, r, c, tr, tc)) {
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
