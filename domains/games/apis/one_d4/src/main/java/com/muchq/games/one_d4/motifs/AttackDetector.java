package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects significant attacks after each move and emits {@link GameFeatures.MotifOccurrence} rows
 * with {@code motif = ATTACK}.
 *
 * <p>An occurrence is emitted when any of:
 *
 * <ul>
 *   <li>Attacked piece is a king (check / checkmate)
 *   <li>Attacked piece is a queen
 *   <li>Same attacker attacks 2+ pieces of value ≥ 2 at this ply (fork)
 *   <li>A discovered attack is revealed (all targets, not just king/queen)
 * </ul>
 *
 * <p>Two categories are detected per move:
 *
 * <ul>
 *   <li><b>Direct</b>: the piece that moved is the attacker ({@code isDiscovered = false})
 *   <li><b>Discovered</b>: a different piece moved, revealing a sliding piece attack ({@code
 *       isDiscovered = true})
 * </ul>
 */
public class AttackDetector implements MotifDetector {

  private final DiscoveredAttackDetector discoveredAttackDetector = new DiscoveredAttackDetector();

  @Override
  public Motif motif() {
    return Motif.ATTACK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> result = new ArrayList<>();

    for (int i = 1; i < positions.size(); i++) {
      PositionContext before = positions.get(i - 1);
      PositionContext after = positions.get(i);
      String move = after.lastMove();
      if (move == null) continue;

      boolean moverIsWhite = !after.whiteToMove();
      boolean isCheckmate = move.endsWith("#");

      int ply = moverIsWhite ? 2 * after.moveNumber() - 1 : 2 * (after.moveNumber() - 1);
      if (ply <= 0) continue;
      String side = moverIsWhite ? "white" : "black";

      int[][] boardBefore = PinDetector.parsePlacement(before.fen().split(" ")[0]);
      int[][] boardAfter = PinDetector.parsePlacement(after.fen().split(" ")[0]);

      // Part 1: direct attacks by the piece that just moved (skip castling)
      if (!move.startsWith("O-")) {
        result.addAll(
            findDirectAttacks(
                boardBefore, boardAfter, moverIsWhite, after.moveNumber(), ply, side, isCheckmate));
      }

      // Part 2: all discovered attacks revealed by the move
      List<DiscoveredAttackDetector.RevealedAttack> revealed =
          discoveredAttackDetector.findDiscoveredAttacks(boardBefore, boardAfter, moverIsWhite);
      for (DiscoveredAttackDetector.RevealedAttack ra : revealed) {
        boolean isMate = isCheckmate && isKing(ra.target());
        String desc = "Discovered attack at move " + after.moveNumber();
        result.add(
            GameFeatures.MotifOccurrence.attack(
                ply,
                after.moveNumber(),
                side,
                desc,
                ra.movedPiece(),
                ra.attacker(),
                ra.target(),
                true,
                isMate));
      }
    }

    return result;
  }

  private static List<GameFeatures.MotifOccurrence> findDirectAttacks(
      int[][] boardBefore,
      int[][] boardAfter,
      boolean moverIsWhite,
      int moveNumber,
      int ply,
      String side,
      boolean isCheckmate) {

    int[] vacated = findVacatedSquare(boardBefore, boardAfter, moverIsWhite);
    if (vacated == null) return List.of();

    int[] dest = findDestSquare(boardBefore, boardAfter, moverIsWhite);
    if (dest == null) return List.of();

    int pieceAtDest = boardAfter[dest[0]][dest[1]];
    if (pieceAtDest == 0) return List.of();

    // Direct attack: pieceMoved == attacker (destination notation for both).
    String attackerNotation =
        DiscoveredAttackDetector.pieceLetter(pieceAtDest)
            + DiscoveredAttackDetector.squareName(dest[0], dest[1]);

    // Collect all enemy pieces attacked by the piece at dest
    List<String> allTargets = new ArrayList<>();
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int ep = boardAfter[r][c];
        if (ep == 0 || (ep > 0) == moverIsWhite) continue;
        if (BoardUtils.pieceAttacks(boardAfter, dest[0], dest[1], r, c)) {
          allTargets.add(
              DiscoveredAttackDetector.pieceLetter(ep) + DiscoveredAttackDetector.squareName(r, c));
        }
      }
    }

    List<String> significant = filterSignificant(allTargets);
    return significant.stream()
        .map(
            t ->
                GameFeatures.MotifOccurrence.attack(
                    ply,
                    moveNumber,
                    side,
                    "Attack at move " + moveNumber,
                    attackerNotation,
                    attackerNotation,
                    t,
                    false,
                    isCheckmate && isKing(t)))
        .toList();
  }

  /** Returns the first square vacated by a mover's piece (occupied before, empty after). */
  private static int[] findVacatedSquare(int[][] before, int[][] after, boolean moverIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pb = before[r][c];
        if (pb != 0 && (pb > 0) == moverIsWhite && after[r][c] == 0) {
          return new int[] {r, c};
        }
      }
    }
    return null;
  }

  /**
   * Returns the first square where a mover's piece appeared that wasn't a mover's piece before (was
   * empty or was an enemy piece).
   */
  private static int[] findDestSquare(int[][] before, int[][] after, boolean moverIsWhite) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        int pa = after[r][c];
        int pb = before[r][c];
        if (pa != 0 && (pa > 0) == moverIsWhite && (pb == 0 || (pb > 0) != moverIsWhite)) {
          return new int[] {r, c};
        }
      }
    }
    return null;
  }

  /**
   * Keeps king/queen targets always; also keeps all targets of value ≥ 2 when the attacker forks 2+
   * such pieces.
   */
  private static List<String> filterSignificant(List<String> targets) {
    List<String> result = new ArrayList<>();
    for (String t : targets) {
      if (isKingOrQueen(t)) result.add(t);
    }
    long valuableCount = targets.stream().filter(t -> pieceValue(t.charAt(0)) >= 2).count();
    if (valuableCount >= 2) {
      for (String t : targets) {
        if (!result.contains(t) && pieceValue(t.charAt(0)) >= 2) {
          result.add(t);
        }
      }
    }
    return result;
  }

  static boolean isKingOrQueen(String t) {
    char c = t.charAt(0);
    return c == 'K' || c == 'k' || c == 'Q' || c == 'q';
  }

  static boolean isKing(String t) {
    char c = t.charAt(0);
    return c == 'K' || c == 'k';
  }

  static int pieceValue(char letter) {
    return switch (Character.toUpperCase(letter)) {
      case 'P' -> 1;
      case 'N' -> 2;
      case 'B' -> 3;
      case 'R' -> 4;
      case 'Q' -> 5;
      case 'K' -> 6;
      default -> 0;
    };
  }
}
