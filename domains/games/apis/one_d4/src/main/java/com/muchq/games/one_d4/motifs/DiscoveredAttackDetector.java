package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class DiscoveredAttackDetector implements MotifDetector {

  /** One revealed attack line from a discovered attack. */
  public record RevealedAttack(String movedPiece, String attacker, String target) {}

  @Override
  public Motif motif() {
    return Motif.DISCOVERED_ATTACK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (int i = 1; i < positions.size(); i++) {
      PositionContext before = positions.get(i - 1);
      PositionContext after = positions.get(i);

      String beforePlacement = before.fen().split(" ")[0];
      String afterPlacement = after.fen().split(" ")[0];
      int[][] boardBefore = BoardUtils.parsePlacement(beforePlacement);
      int[][] boardAfter = BoardUtils.parsePlacement(afterPlacement);

      boolean moverIsWhite = !after.whiteToMove();

      List<RevealedAttack> attacks = findDiscoveredAttacks(boardBefore, boardAfter, moverIsWhite);
      for (RevealedAttack ra : attacks) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.discoveredAttack(
                after,
                "Discovered attack at move " + after.moveNumber(),
                ra.movedPiece(),
                ra.attacker(),
                ra.target());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /**
   * Find all discovered attacks in a before/after board pair. Public so that {@link
   * DiscoveredCheckDetector} can reuse the logic.
   */
  public List<RevealedAttack> findDiscoveredAttacks(
      int[][] before, int[][] after, boolean moverIsWhite) {
    List<RevealedAttack> result = new ArrayList<>();
    List<int[]> landed = BoardUtils.landedSquares(before, after, moverIsWhite);
    // One move can empty two squares on a single ray — an en passant capture
    // takes a pawn beside the square its captor left — and the same
    // discovery reported twice reads downstream as two attackers on one
    // king, which is how a false DOUBLE_CHECK gets derived.
    Set<String> seen = new LinkedHashSet<>();

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pieceBefore = before[r][c];
        if (pieceBefore == 0 || after[r][c] != 0) continue;

        // Every square the move emptied, not just the mover's own. An en
        // passant capture empties a third square — the taken pawn's, which
        // is on neither square the move names — and the lines through it
        // open with nothing of ours near them.
        boolean ourPiece = (pieceBefore > 0) == moverIsWhite;
        String movedPiece;
        if (ourPiece) {
          // Where this piece went. Null for a promotion: no square holds a
          // pawn afterwards, because the pawn became something else.
          int[] samePiece = findDestinationCoords(before, after, pieceBefore, r, c);
          movedPiece =
              pieceLetter(pieceBefore)
                  + squareName(r, c)
                  + (samePiece != null ? squareName(samePiece[0], samePiece[1]) : "??");
        } else {
          // The pawn taken en passant. What moved is the pawn that took it.
          int[] from = moverOrigin(before, after, moverIsWhite);
          int[] to = nearest(landed, r, c);
          if (from == null || to == null) continue;
          movedPiece =
              pieceLetter(before[from[0]][from[1]])
                  + squareName(from[0], from[1])
                  + squareName(to[0], to[1]);
        }
        for (RevealedAttack revealed :
            revealsAttacks(after, r, c, moverIsWhite, movedPiece, landed)) {
          if (seen.add(revealed.attacker() + ">" + revealed.target())) result.add(revealed);
        }
      }
    }
    return result;
  }

  /** The landing square nearest (row, col) — the one this emptied square belongs to. */
  private static int[] nearest(List<int[]> landed, int row, int col) {
    int[] best = null;
    int bestDistance = Integer.MAX_VALUE;
    for (int[] square : landed) {
      int distance = Math.max(Math.abs(square[0] - row), Math.abs(square[1] - col));
      if (distance < bestDistance) {
        bestDistance = distance;
        best = square;
      }
    }
    return best;
  }

  /** The first square the mover emptied of one of its own pieces. */
  private static int[] moverOrigin(int[][] before, int[][] after, boolean moverIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int piece = before[r][c];
        if (piece != 0 && (piece > 0) == moverIsWhite && after[r][c] == 0) {
          return new int[] {r, c};
        }
      }
    }
    return null;
  }

  private List<RevealedAttack> revealsAttacks(
      int[][] board,
      int vacatedR,
      int vacatedC,
      boolean moverIsWhite,
      String movedPiece,
      List<int[]> landed) {
    int[][] directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    List<RevealedAttack> attacks = new ArrayList<>();

    for (int[] dir : directions) {
      int br = vacatedR - dir[0], bc = vacatedC - dir[1];
      while (br >= 0 && br < 8 && bc >= 0 && bc < 8) {
        int piece = board[br][bc];
        if (piece != 0) {
          // A piece this move put down is not one it uncovered. Every
          // landing square, not just the one paired with this origin:
          // castling empties two squares and fills two, and a ray out of
          // the king's old square finds the rook on its new one.
          if (BoardUtils.isLanded(landed, br, bc)) {
            break;
          }
          boolean isWhite = piece > 0;
          if (isWhite == moverIsWhite && isSlidingAttacker(piece, dir)) {
            int fr = vacatedR + dir[0], fc = vacatedC + dir[1];
            while (fr >= 0 && fr < 8 && fc >= 0 && fc < 8) {
              int targetPiece = board[fr][fc];
              if (targetPiece != 0) {
                boolean targetIsWhite = targetPiece > 0;
                if (targetIsWhite != moverIsWhite) {
                  String attackerStr = pieceLetter(piece) + squareName(br, bc);
                  String targetStr = pieceLetter(targetPiece) + squareName(fr, fc);
                  attacks.add(new RevealedAttack(movedPiece, attackerStr, targetStr));
                }
                break;
              }
              fr += dir[0];
              fc += dir[1];
            }
          }
          break;
        }
        br -= dir[0];
        bc -= dir[1];
      }
    }
    return attacks;
  }

  private int[] findDestinationCoords(
      int[][] before, int[][] after, int piece, int fromR, int fromC) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        if (r == fromR && c == fromC) continue;
        if (after[r][c] == piece && before[r][c] != piece) {
          return new int[] {r, c};
        }
      }
    }
    return null; // promotions or complex cases
  }

  private boolean isSlidingAttacker(int piece, int[] dir) {
    int absPiece = Math.abs(piece);
    boolean isDiagonal = dir[0] != 0 && dir[1] != 0;
    boolean isStraight = dir[0] == 0 || dir[1] == 0;
    if (absPiece == 5) return true;
    if (absPiece == 3 && isDiagonal) return true;
    if (absPiece == 4 && isStraight) return true;
    return false;
  }

  static String pieceLetter(int piece) {
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
    return String.valueOf(white ? letter : Character.toLowerCase(letter));
  }

  static String squareName(int row, int col) {
    // row 0 = rank 8, row 7 = rank 1; col 0 = file a
    char file = (char) ('a' + col);
    char rank = (char) ('8' - row);
    return "" + file + rank;
  }
}
