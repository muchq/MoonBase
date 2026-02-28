package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

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
      int[][] boardBefore = PinDetector.parsePlacement(beforePlacement);
      int[][] boardAfter = PinDetector.parsePlacement(afterPlacement);

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

    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pieceBefore = before[r][c];
        int pieceAfter = after[r][c];

        if (pieceBefore != 0 && pieceAfter == 0) {
          boolean isWhite = pieceBefore > 0;
          if (isWhite == moverIsWhite) {
            int[] dest = findDestinationCoords(before, after, pieceBefore, r, c);
            String destSquare = dest != null ? squareName(dest[0], dest[1]) : "??";
            String movedPiece = pieceLetter(pieceBefore) + squareName(r, c) + destSquare;
            result.addAll(revealsAttacks(after, r, c, moverIsWhite, movedPiece, dest));
          }
        }
      }
    }
    return result;
  }

  private List<RevealedAttack> revealsAttacks(
      int[][] board,
      int vacatedR,
      int vacatedC,
      boolean moverIsWhite,
      String movedPiece,
      int[] dest) {
    int[][] directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    List<RevealedAttack> attacks = new ArrayList<>();

    for (int[] dir : directions) {
      int br = vacatedR - dir[0], bc = vacatedC - dir[1];
      while (br >= 0 && br < 8 && bc >= 0 && bc < 8) {
        int piece = board[br][bc];
        if (piece != 0) {
          // The moved piece at its destination is not a revealed attacker
          if (dest != null && br == dest[0] && bc == dest[1]) {
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
