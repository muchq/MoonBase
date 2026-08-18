package com.muchq.games.one_d4.motifs;

import java.util.ArrayList;
import java.util.List;

/**
 * Shared board analysis utilities for motif detectors and the SAN replay engine ({@code
 * ReplayBoard}). All coordinates use the board array convention where board[0][0] = a8 (rank 8,
 * file a) and board[7][7] = h1 (rank 1, file h). Piece values: P=1, N=2, B=3, R=4, Q=5, K=6;
 * negative for black pieces.
 */
public class BoardUtils {

  private BoardUtils() {}

  /**
   * Returns true if the piece at (pieceRow, pieceCol) attacks the square (targetRow, targetCol).
   * Handles all piece types including path-clearing for sliding pieces.
   */
  public static boolean pieceAttacks(
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

  public static int[][] parsePlacement(String placement) {
    int[][] board = new int[8][8];
    String[] ranks = placement.split("/");
    for (int r = 0; r < 8; r++) {
      int c = 0;
      for (char ch : ranks[r].toCharArray()) {
        if (Character.isDigit(ch)) {
          c += ch - '0';
        } else {
          board[r][c] = pieceValue(ch);
          c++;
        }
      }
    }
    return board;
  }

  public static int pieceValue(char ch) {
    return switch (ch) {
      case 'K' -> 6;
      case 'Q' -> 5;
      case 'R' -> 4;
      case 'B' -> 3;
      case 'N' -> 2;
      case 'P' -> 1;
      case 'k' -> -6;
      case 'q' -> -5;
      case 'r' -> -4;
      case 'b' -> -3;
      case 'n' -> -2;
      case 'p' -> -1;
      default -> 0;
    };
  }

  /** Returns true if all squares strictly between (fromRow,fromCol) and (toRow,toCol) are empty. */
  public static boolean isPathClear(int[][] board, int fromRow, int fromCol, int toRow, int toCol) {
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
   * Finds the row of the king of the given color. Returns -1 if not found. Stores the result in a
   * two-element array {row, col}.
   */
  public static int[] findKing(int[][] board, boolean kingIsWhite) {
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
  public static String squareName(int row, int col) {
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
  public static int[] findCheckingPiece(int[][] board, boolean moverIsWhite) {
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
   * Every square the move put a piece on, read from the notation.
   *
   * <p>Two squares for castling — the king's and the rook's — because the rook lands on a new file
   * where it can pin, skewer and attack. The old reader returned null for castling with a comment
   * that the king cannot create a sliding tactic, which is true of the king and false of the rook
   * beside it. "O-O" names no square, but the pair it moves is fixed, so the notation is enough.
   *
   * <p>One square for everything else, including a promotion (where the new piece stands) and an en
   * passant capture (where the pawn ended up, not where the taken pawn was).
   */
  public static List<int[]> landedSquares(String san, boolean moverIsWhite) {
    if (san == null) return List.of();
    int backRank = moverIsWhite ? 7 : 0; // row 7 is rank 1
    if (san.startsWith("O-O-O")) {
      return List.of(new int[] {backRank, 2}, new int[] {backRank, 3}); // king c-file, rook d-file
    }
    if (san.startsWith("O-O")) {
      return List.of(new int[] {backRank, 6}, new int[] {backRank, 5}); // king g-file, rook f-file
    }
    String move = san.replaceAll("[+#]|=[QRBN]", "");
    if (move.length() < 2) return List.of();
    String square = move.substring(move.length() - 2);
    char file = square.charAt(0);
    char rank = square.charAt(1);
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return List.of();
    return List.of(new int[] {'8' - rank, file - 'a'});
  }

  /**
   * The same question asked of two boards rather than of the notation, for callers that have the
   * boards and not the move: occupied by the mover after, and not before.
   */
  public static List<int[]> landedSquares(int[][] before, int[][] after, boolean moverIsWhite) {
    List<int[]> landed = new ArrayList<>();
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pa = after[r][c];
        int pb = before[r][c];
        if (pa != 0 && (pa > 0) == moverIsWhite && (pb == 0 || (pb > 0) != moverIsWhite)) {
          landed.add(new int[] {r, c});
        }
      }
    }
    return landed;
  }

  /** True when {@code landed} holds the square (row, col). */
  public static boolean isLanded(List<int[]> landed, int row, int col) {
    for (int[] square : landed) {
      if (square[0] == row && square[1] == col) return true;
    }
    return false;
  }

  /** True when {@code landed} holds the square named by {@code squareName}, e.g. "f1". */
  public static boolean isLanded(List<int[]> landed, String square) {
    for (int[] s : landed) {
      if (squareName(s[0], s[1]).equals(square)) return true;
    }
    return false;
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
