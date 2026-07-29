package com.muchq.games.one_d4.engine;

import com.muchq.games.one_d4.motifs.BoardUtils;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A minimal mutable board that applies SAN moves and emits FEN, sharing movement rules with the
 * motif detectors via {@link BoardUtils}. It exists purely for replay speed: chariot's immutable
 * {@code Board.play(san)} costs ~0.8ms per move — 99% of feature-extraction CPU as measured at
 * introduction — while this implementation applies a move in microseconds. Chariot remains the
 * correctness oracle in tests (see GameReplayerParityTest).
 *
 * <p>Assumes legal SAN input (chess.com PGNs), with guards that throw rather than corrupt the
 * position when the input is malformed. Disambiguation follows SAN semantics: when several
 * pseudo-legal candidates remain after any file/rank hint, the single one whose move does not leave
 * its own king in check is chosen.
 */
final class ReplayBoard {

  // Core SAN plus any trailing check/mate/annotation suffixes and the optional "e.p." marker.
  private static final Pattern SAN =
      Pattern.compile(
          "^([KQRBN])?([a-h])?([1-8])?(x)?([a-h][1-8])(?:=?([QRBN]))?(?:\\s*e\\.p\\.|[+#!?])*$");

  // board[0][0] = a8, board[7][7] = h1; piece values per BoardUtils (P=1..K=6, negative = black)
  private final int[][] board;
  private boolean whiteToMove = true;
  private boolean whiteKingside = true;
  private boolean whiteQueenside = true;
  private boolean blackKingside = true;
  private boolean blackQueenside = true;
  private int epRow = -1;
  private int epCol = -1;
  private int halfmoveClock = 0;
  private int fullmoveNumber = 1;

  private ReplayBoard(int[][] board) {
    this.board = board;
  }

  static ReplayBoard standard() {
    return new ReplayBoard(
        BoardUtils.parsePlacement("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"));
  }

  /** Test-only: builds a board from a full FEN string. */
  static ReplayBoard fromFen(String fen) {
    String[] fields = fen.strip().split("\\s+");
    ReplayBoard replayBoard = new ReplayBoard(BoardUtils.parsePlacement(fields[0]));
    replayBoard.whiteToMove = fields.length < 2 || "w".equals(fields[1]);
    String castling = fields.length < 3 ? "-" : fields[2];
    replayBoard.whiteKingside = castling.contains("K");
    replayBoard.whiteQueenside = castling.contains("Q");
    replayBoard.blackKingside = castling.contains("k");
    replayBoard.blackQueenside = castling.contains("q");
    if (fields.length >= 4 && !"-".equals(fields[3])) {
      replayBoard.epCol = fields[3].charAt(0) - 'a';
      replayBoard.epRow = 8 - (fields[3].charAt(1) - '0');
    }
    if (fields.length >= 5) {
      replayBoard.halfmoveClock = Integer.parseInt(fields[4]);
    }
    if (fields.length >= 6) {
      replayBoard.fullmoveNumber = Integer.parseInt(fields[5]);
    }
    return replayBoard;
  }

  /** Applies one SAN move for the side to move. */
  void play(String san) {
    boolean white = whiteToMove;
    boolean resetClock;

    if (san.startsWith("O-O-O")) {
      castle(white, false);
      resetClock = false;
    } else if (san.startsWith("O-O")) {
      castle(white, true);
      resetClock = false;
    } else {
      Matcher m = SAN.matcher(san);
      if (!m.matches()) {
        throw new IllegalArgumentException("Unparseable SAN: " + san);
      }
      String pieceLetter = m.group(1);
      String fileHint = m.group(2);
      String rankHint = m.group(3);
      boolean capture = m.group(4) != null;
      String target = m.group(5);
      String promotion = m.group(6);

      int toCol = target.charAt(0) - 'a';
      int toRow = 8 - (target.charAt(1) - '0');

      if (pieceLetter == null) {
        playPawnMove(white, fileHint, capture, toRow, toCol, promotion);
        resetClock = true;
      } else {
        resetClock =
            playPieceMove(
                white,
                BoardUtils.pieceValue(pieceLetter.charAt(0)),
                rankHint == null ? -1 : 8 - (rankHint.charAt(0) - '0'),
                fileHint == null ? -1 : fileHint.charAt(0) - 'a',
                toRow,
                toCol);
      }
    }

    halfmoveClock = resetClock ? 0 : halfmoveClock + 1;
    if (!white) {
      fullmoveNumber++;
    }
    whiteToMove = !white;
  }

  String toFEN() {
    StringBuilder sb = new StringBuilder(80);
    for (int r = 0; r < 8; r++) {
      int empty = 0;
      for (int c = 0; c < 8; c++) {
        int piece = board[r][c];
        if (piece == 0) {
          empty++;
          continue;
        }
        if (empty > 0) {
          sb.append(empty);
          empty = 0;
        }
        sb.append(pieceChar(piece));
      }
      if (empty > 0) {
        sb.append(empty);
      }
      if (r < 7) {
        sb.append('/');
      }
    }
    sb.append(whiteToMove ? " w " : " b ");
    if (whiteKingside || whiteQueenside || blackKingside || blackQueenside) {
      if (whiteKingside) sb.append('K');
      if (whiteQueenside) sb.append('Q');
      if (blackKingside) sb.append('k');
      if (blackQueenside) sb.append('q');
    } else {
      sb.append('-');
    }
    sb.append(' ');
    if (epRow >= 0) {
      sb.append((char) ('a' + epCol)).append((char) ('8' - epRow));
    } else {
      sb.append('-');
    }
    sb.append(' ').append(halfmoveClock).append(' ').append(fullmoveNumber);
    return sb.toString();
  }

  private void castle(boolean white, boolean kingside) {
    int row = white ? 7 : 0;
    if (kingside) {
      board[row][6] = board[row][4]; // king e→g
      board[row][5] = board[row][7]; // rook h→f
      board[row][4] = 0;
      board[row][7] = 0;
    } else {
      board[row][2] = board[row][4]; // king e→c
      board[row][3] = board[row][0]; // rook a→d
      board[row][4] = 0;
      board[row][0] = 0;
    }
    clearCastlingRights(white);
    clearEnPassant();
  }

  private void playPawnMove(
      boolean white, String fileHint, boolean capture, int toRow, int toCol, String promotion) {
    int pawn = white ? 1 : -1;
    // White pawns move toward row 0 (rank 8), so the origin square is one row BELOW the
    // destination in array terms. Note: opposite sign of BoardUtils.pieceAttacks' pawnDir, which
    // points in the forward direction.
    int originRowOffset = white ? 1 : -1;
    int fromRow;
    int fromCol;
    boolean doublePush = false;

    if (capture) {
      if (fileHint == null) {
        throw new IllegalArgumentException(
            "Pawn capture without file: " + BoardUtils.squareName(toRow, toCol));
      }
      fromCol = fileHint.charAt(0) - 'a';
      fromRow = toRow + originRowOffset;
      if (board[toRow][toCol] == 0) {
        // en passant: the captured pawn sits on the origin row, destination file
        if (board[fromRow][toCol] != -pawn) {
          throw new IllegalArgumentException(
              "No piece to capture on " + BoardUtils.squareName(toRow, toCol));
        }
        board[fromRow][toCol] = 0;
      } else {
        clearRookRights(toRow, toCol);
      }
    } else {
      fromCol = toCol;
      if (board[toRow + originRowOffset][toCol] == pawn) {
        fromRow = toRow + originRowOffset;
      } else {
        if (board[toRow + originRowOffset][toCol] != 0) {
          throw new IllegalArgumentException(
              "Blocked pawn push to " + BoardUtils.squareName(toRow, toCol));
        }
        fromRow = toRow + 2 * originRowOffset;
        // A double push can only originate from the pawn's home rank
        if (fromRow != (white ? 6 : 1)) {
          throw new IllegalArgumentException(
              "No pawn found for move to " + BoardUtils.squareName(toRow, toCol));
        }
        doublePush = true;
      }
    }
    if (board[fromRow][fromCol] != pawn) {
      throw new IllegalArgumentException(
          "No pawn found for move to " + BoardUtils.squareName(toRow, toCol));
    }

    board[toRow][toCol] =
        promotion == null ? pawn : pawn * BoardUtils.pieceValue(promotion.charAt(0));
    board[fromRow][fromCol] = 0;

    if (doublePush) {
      epRow = toRow + originRowOffset;
      epCol = toCol;
    } else {
      clearEnPassant();
    }
  }

  /** Returns true when the move captured a piece (the caller resets the halfmove clock). */
  private boolean playPieceMove(
      boolean white, int pieceType, int fromRowHint, int fromColHint, int toRow, int toCol) {
    int piece = white ? pieceType : -pieceType;
    List<int[]> candidates = new ArrayList<>(2);
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        if (board[r][c] != piece) continue;
        if (fromRowHint >= 0 && r != fromRowHint) continue;
        if (fromColHint >= 0 && c != fromColHint) continue;
        if (BoardUtils.pieceAttacks(board, r, c, toRow, toCol)) {
          candidates.add(new int[] {r, c});
        }
      }
    }

    int[] from = null;
    if (candidates.size() == 1) {
      from = candidates.get(0);
    } else {
      // SAN omits disambiguation when only one candidate is legal (e.g. the other is pinned);
      // two legal candidates without a resolving hint is malformed input.
      for (int[] candidate : candidates) {
        if (isLegal(candidate[0], candidate[1], toRow, toCol, white)) {
          if (from != null) {
            throw new IllegalArgumentException(
                "Ambiguous move to " + BoardUtils.squareName(toRow, toCol));
          }
          from = candidate;
        }
      }
    }
    if (from == null) {
      throw new IllegalArgumentException(
          "No candidate found for move to " + BoardUtils.squareName(toRow, toCol));
    }

    boolean captured = board[toRow][toCol] != 0;
    if (captured) {
      clearRookRights(toRow, toCol);
    }
    board[toRow][toCol] = piece;
    board[from[0]][from[1]] = 0;

    if (pieceType == 6) { // a king move forfeits both castling rights
      clearCastlingRights(white);
    } else if (pieceType == 4) { // a rook leaving its home square forfeits that wing's right
      clearRookRights(from[0], from[1]);
    }
    clearEnPassant();
    return captured;
  }

  /** True when moving (fromRow,fromCol) → (toRow,toCol) leaves the mover's king unattacked. */
  private boolean isLegal(int fromRow, int fromCol, int toRow, int toCol, boolean white) {
    int moved = board[fromRow][fromCol];
    int captured = board[toRow][toCol];
    board[toRow][toCol] = moved;
    board[fromRow][fromCol] = 0;
    try {
      return BoardUtils.findCheckingPiece(board, !white) == null;
    } finally {
      board[fromRow][fromCol] = moved;
      board[toRow][toCol] = captured;
    }
  }

  private void clearEnPassant() {
    epRow = -1;
    epCol = -1;
  }

  private void clearCastlingRights(boolean white) {
    if (white) {
      whiteKingside = false;
      whiteQueenside = false;
    } else {
      blackKingside = false;
      blackQueenside = false;
    }
  }

  /**
   * Clears the castling right whose rook home square is (row, col); no-op for other squares. Safe
   * for any move touching a corner: if the original rook already left it, that right is already
   * gone.
   */
  private void clearRookRights(int row, int col) {
    if (row == 7 && col == 0) whiteQueenside = false;
    if (row == 7 && col == 7) whiteKingside = false;
    if (row == 0 && col == 0) blackQueenside = false;
    if (row == 0 && col == 7) blackKingside = false;
  }

  private static char pieceChar(int piece) {
    char c =
        switch (Math.abs(piece)) {
          case 1 -> 'p';
          case 2 -> 'n';
          case 3 -> 'b';
          case 4 -> 'r';
          case 5 -> 'q';
          case 6 -> 'k';
          default -> '?';
        };
    return piece > 0 ? Character.toUpperCase(c) : c;
  }
}
