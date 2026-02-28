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
      int[][] board = parsePlacement(placement);

      // Detect absolute pins (to the king of side to move)
      // and relative pins (to another valuable piece)
      List<PinData> pins = detectPins(board, ctx.whiteToMove());
      for (PinData pin : pins) {
        String desc = "Pin detected at move " + ctx.moveNumber();
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.pin(
                ctx, desc, pin.attacker(), pin.target(), pin.pinType());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }

  /**
   * Finds all pins on the board for the side to move. Returns one PinData per pin found. Absolute
   * pins are detected by ray-casting from the king. Relative pins are detected by scanning enemy
   * sliding pieces for alignments with two friendly pieces.
   */
  private List<PinData> detectPins(int[][] board, boolean whiteToMove) {
    List<PinData> pins = new ArrayList<>();
    int[][] directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    // === Absolute pins: ray from king ===
    int[] kingPos = BoardUtils.findKing(board, whiteToMove);
    if (kingPos[0] != -1) {
      for (int[] dir : directions) {
        PinData pin =
            findPinAlongRay(board, kingPos[0], kingPos[1], dir[0], dir[1], whiteToMove, true);
        if (pin != null) pins.add(pin);
      }
    }

    // === Relative pins: scan enemy sliding pieces, ray toward each valuable friendly ===
    for (int er = 0; er < 8; er++) {
      for (int ec = 0; ec < 8; ec++) {
        int enemyPiece = board[er][ec];
        if (enemyPiece == 0) continue;
        if ((enemyPiece > 0) == whiteToMove) continue; // must be enemy piece
        int absEnemy = Math.abs(enemyPiece);
        if (absEnemy != 3 && absEnemy != 4 && absEnemy != 5) continue; // sliding only

        for (int[] dir : directions) {
          if (!canAttackDirection(absEnemy, dir)) continue;

          // Scan along the ray from the enemy piece
          int r = er + dir[0], c = ec + dir[1];
          int pinnedR = -1, pinnedC = -1;
          boolean foundFriendly = false;

          while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            int piece = board[r][c];
            if (piece != 0) {
              boolean isWhite = piece > 0;
              if (isWhite == whiteToMove) {
                // Friendly piece
                if (!foundFriendly) {
                  foundFriendly = true;
                  pinnedR = r;
                  pinnedC = c;
                } else {
                  // Second friendly piece — check if it's valuable enough to make a relative pin
                  int anchor = Math.abs(piece);
                  int pinned = Math.abs(board[pinnedR][pinnedC]);
                  // Relative pin: anchor piece is more valuable than the "pinned" piece
                  if (anchor > pinned && anchor >= 4) {
                    // Avoid duplicating absolute pins (king is handled separately)
                    if (anchor != 6) {
                      String attacker = BoardUtils.pieceNotation(enemyPiece, er, ec);
                      String target =
                          BoardUtils.pieceNotation(board[pinnedR][pinnedC], pinnedR, pinnedC);
                      pins.add(new PinData(attacker, target, "RELATIVE"));
                    }
                  }
                  break;
                }
              } else {
                // Hit another enemy piece — no pin along this ray
                break;
              }
            }
            r += dir[0];
            c += dir[1];
          }
        }
      }
    }

    // Deduplicate: remove any RELATIVE pin where absolute pin already covers same pinnedSquare
    pins = deduplicatePins(pins);
    return pins;
  }

  /**
   * Ray-casts from (anchorR, anchorC) in direction (dr, dc). If it finds exactly one friendly piece
   * followed by an enemy sliding attacker compatible with the direction, returns PinData.
   */
  private PinData findPinAlongRay(
      int[][] board,
      int anchorR,
      int anchorC,
      int dr,
      int dc,
      boolean friendlyIsWhite,
      boolean isAbsolute) {
    int r = anchorR + dr, c = anchorC + dc;
    int pinnedR = -1, pinnedC = -1;
    boolean foundFriendly = false;

    while (r >= 0 && r < 8 && c >= 0 && c < 8) {
      int piece = board[r][c];
      if (piece != 0) {
        boolean isWhite = piece > 0;
        if (isWhite == friendlyIsWhite) {
          // Friendly piece
          if (foundFriendly) return null; // two friendly pieces blocks any pin
          foundFriendly = true;
          pinnedR = r;
          pinnedC = c;
        } else {
          // Enemy piece — check if it's a sliding attacker on this line
          if (foundFriendly && isSlidingAttacker(piece, dr, dc)) {
            String attacker = BoardUtils.pieceNotation(piece, r, c);
            String target = BoardUtils.pieceNotation(board[pinnedR][pinnedC], pinnedR, pinnedC);
            return new PinData(attacker, target, isAbsolute ? "ABSOLUTE" : "RELATIVE");
          }
          return null;
        }
      }
      r += dr;
      c += dc;
    }
    return null;
  }

  private boolean isSlidingAttacker(int piece, int dr, int dc) {
    int absPiece = Math.abs(piece);
    boolean isDiagonal = dr != 0 && dc != 0;
    boolean isStraight = dr == 0 || dc == 0;
    if (absPiece == 5) return true;
    if (absPiece == 3 && isDiagonal) return true;
    if (absPiece == 4 && isStraight) return true;
    return false;
  }

  private boolean canAttackDirection(int absEnemyPiece, int[] dir) {
    boolean isDiagonal = dir[0] != 0 && dir[1] != 0;
    boolean isStraight = dir[0] == 0 || dir[1] == 0;
    if (absEnemyPiece == 5) return true; // queen
    if (absEnemyPiece == 3) return isDiagonal; // bishop
    if (absEnemyPiece == 4) return isStraight; // rook
    return false;
  }

  /** Remove duplicate pins (same attacker+target appearing twice). */
  private List<PinData> deduplicatePins(List<PinData> pins) {
    List<PinData> result = new ArrayList<>();
    for (PinData pin : pins) {
      boolean duplicate = false;
      for (PinData existing : result) {
        if (existing.attacker().equals(pin.attacker()) && existing.target().equals(pin.target())) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) result.add(pin);
    }
    return result;
  }

  record PinData(String attacker, String target, String pinType) {}

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
