package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class PinDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.PIN;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String fen = ctx.fen();
      String placement = fen.split(" ")[0];
      if (detectPinFromFen(placement, ctx.whiteToMove())) {
        occurrences.add(
            new GameFeatures.MotifOccurrence(
                ctx.moveNumber(), "Pin detected at move " + ctx.moveNumber()));
      }
    }

    return occurrences;
  }

  private boolean detectPinFromFen(String placement, boolean whiteToMove) {
    // Find king position and check for pieces on diagonals/files/ranks
    // between sliding attackers and the king.
    // This is a heuristic approach - full implementation would use
    // ray-casting from the king position.
    int[][] boardArray = parsePlacement(placement);
    int kingRow = -1, kingCol = -1;
    int kingPiece = whiteToMove ? 6 : -6; // K=6, k=-6

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        if (boardArray[r][c] == kingPiece) {
          kingRow = r;
          kingCol = c;
        }
      }
    }

    if (kingRow == -1) return false;

    // Check all 8 directions from the king for pins
    int[][] directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (int[] dir : directions) {
      if (isPinAlongRay(boardArray, kingRow, kingCol, dir[0], dir[1], whiteToMove)) {
        return true;
      }
    }
    return false;
  }

  private boolean isPinAlongRay(int[][] board, int kr, int kc, int dr, int dc, boolean whiteKing) {
    int friendlyPieceCount = 0;
    int r = kr + dr, c = kc + dc;
    boolean foundFriendly = false;

    while (r >= 0 && r < 8 && c >= 0 && c < 8) {
      int piece = board[r][c];
      if (piece != 0) {
        boolean isWhitePiece = piece > 0;
        if (isWhitePiece == whiteKing) {
          // Friendly piece
          friendlyPieceCount++;
          if (friendlyPieceCount > 1) return false;
          foundFriendly = true;
        } else {
          // Enemy piece - check if it's a sliding attacker on this line
          if (foundFriendly && isSlidingAttacker(piece, dr, dc)) {
            return true;
          }
          return false;
        }
      }
      r += dr;
      c += dc;
    }
    return false;
  }

  private boolean isSlidingAttacker(int piece, int dr, int dc) {
    int absPiece = Math.abs(piece);
    boolean isDiagonal = dr != 0 && dc != 0;
    boolean isStraight = dr == 0 || dc == 0;

    // Queen (5) attacks on both diagonals and straights
    if (absPiece == 5) return true;
    // Bishop (3) attacks on diagonals
    if (absPiece == 3 && isDiagonal) return true;
    // Rook (4) attacks on straight lines
    if (absPiece == 4 && isStraight) return true;

    return false;
  }

  static int[][] parsePlacement(String placement) {
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

  static int pieceValue(char ch) {
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
}
