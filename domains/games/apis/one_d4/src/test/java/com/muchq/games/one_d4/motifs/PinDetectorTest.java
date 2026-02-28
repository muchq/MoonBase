package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class PinDetectorTest {

  private final PinDetector detector = new PinDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.PIN);
  }

  // === Absolute pins (to king) ===

  @Test
  public void absolutePin_rookPinsKnightToKing() {
    // Black rook on a4 pins white knight on e4 to white king on h4
    String fen = "8/8/8/8/r3N2K/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(15, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.pinType()).isEqualTo("ABSOLUTE");
    // attacker = black rook on a4, target = white knight on e4
    assertThat(occ.attacker()).isEqualTo("ra4");
    assertThat(occ.target()).isEqualTo("Ne4");
  }

  @Test
  public void absolutePin_bishopPinsBishopToKing() {
    // Black bishop on a1 pins white bishop on d4 to white king on g7
    String fen = "8/6K1/8/8/3B4/8/8/b6k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(20, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).pinType()).isEqualTo("ABSOLUTE");
    assertThat(occurrences.get(0).attacker()).isEqualTo("ba1");
    assertThat(occurrences.get(0).target()).isEqualTo("Bd4");
  }

  @Test
  public void absolutePin_queenPinsRookToKing() {
    // Black queen on a8 pins white rook on d8 to white king on h8
    String fen = "q2R3K/8/8/8/8/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(25, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).pinType()).isEqualTo("ABSOLUTE");
    assertThat(occurrences.get(0).attacker()).isEqualTo("qa8");
    assertThat(occurrences.get(0).target()).isEqualTo("Rd8");
  }

  @Test
  public void absolutePin_blackPieceIsPinned() {
    // White rook on a5 pins black knight on e5 to black king on h5
    String fen = "8/8/8/R3n2k/8/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(18, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).pinType()).isEqualTo("ABSOLUTE");
    assertThat(occurrences.get(0).attacker()).isEqualTo("Ra5");
    assertThat(occurrences.get(0).target()).isEqualTo("ne5");
  }

  @Test
  public void absolutePin_diagonalPin() {
    // White bishop on b1 pins black knight on d3 to black king on f5
    String fen = "8/8/8/5k2/8/3n4/8/1B2K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(12, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).pinType()).isEqualTo("ABSOLUTE");
    assertThat(occurrences.get(0).attacker()).isEqualTo("Bb1");
    assertThat(occurrences.get(0).target()).isEqualTo("nd3");
  }

  @Test
  public void absolutePin_multipleSimultaneousPins() {
    // Two simultaneous absolute pins on white king at h8 (row 0, col 7):
    // 1. Black rook a8 pins white knight e8 along rank 8
    // 2. Black bishop a1 pins white rook e5 along the a1-h8 diagonal
    // Black king at h2 (not interfering).
    String fen = "r3N2K/8/8/4R3/8/8/7k/b7 w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    // Both absolute pins detected
    assertThat(occurrences).hasSizeGreaterThanOrEqualTo(2);
    assertThat(occurrences).allMatch(o -> "ABSOLUTE".equals(o.pinType()));
    // Pin 1: black rook a8 pins white knight e8
    assertThat(occurrences).anyMatch(o -> "ra8".equals(o.attacker()) && "Ne8".equals(o.target()));
    // Pin 2: black bishop a1 pins white rook e5
    assertThat(occurrences).anyMatch(o -> "ba1".equals(o.attacker()) && "Re5".equals(o.target()));
  }

  // === Relative pins ===

  @Test
  public void relativePin_rookPinsKnightToQueen() {
    // Black rook on a5, white knight on e5, white queen on h5 (behind knight)
    // Knight is relatively pinned to queen
    String fen = "8/8/8/r3N2Q/8/8/8/4K2k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(12, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    boolean hasRelative = occurrences.stream().anyMatch(o -> "RELATIVE".equals(o.pinType()));
    assertThat(hasRelative).isTrue();
    GameFeatures.MotifOccurrence rel =
        occurrences.stream().filter(o -> "RELATIVE".equals(o.pinType())).findFirst().orElseThrow();
    assertThat(rel.attacker()).isEqualTo("ra5");
    assertThat(rel.target()).isEqualTo("Ne5");
  }

  // === No pin cases ===

  @Test
  public void noPin_startingPosition() {
    String fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(1, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void noPin_noPieceBetweenAttackerAndKing() {
    // Black rook attacks white king directly, no pin
    String fen = "8/8/8/8/r6K/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void noPin_twoPiecesBetweenAttackerAndKing() {
    // Black rook, two white pieces, white king - not a pin
    String fen = "8/8/8/8/r2NN2K/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void noPin_knightCannotPin() {
    // Knights cannot create pins (don't slide)
    String fen = "8/8/2n5/8/3B4/8/8/4K2k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void noPin_wrongPieceType() {
    // Rook on diagonal cannot pin (rooks don't attack diagonally)
    String fen = "8/6K1/8/8/3B4/8/8/r6k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void noPin_emptyPositionList() {
    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of());
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void pin_occurrenceHasNullMovedPiece() {
    String fen = "8/8/8/8/r3N2K/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(15, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).movedPiece()).isNull();
    assertThat(occurrences.get(0).isDiscovered()).isFalse();
    assertThat(occurrences.get(0).isMate()).isFalse();
  }

  // === Helper methods ===

  @Test
  public void parsePlacement_startingPosition() {
    String placement = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
    int[][] board = PinDetector.parsePlacement(placement);

    assertThat(board[0][0]).isEqualTo(-4); // black rook a8
    assertThat(board[0][4]).isEqualTo(-6); // black king e8
    assertThat(board[7][4]).isEqualTo(6); // white king e1
    assertThat(board[7][3]).isEqualTo(5); // white queen d1
    assertThat(board[4][4]).isEqualTo(0); // empty e4
  }

  @Test
  public void pieceValue_allPieces() {
    assertThat(PinDetector.pieceValue('K')).isEqualTo(6);
    assertThat(PinDetector.pieceValue('Q')).isEqualTo(5);
    assertThat(PinDetector.pieceValue('R')).isEqualTo(4);
    assertThat(PinDetector.pieceValue('B')).isEqualTo(3);
    assertThat(PinDetector.pieceValue('N')).isEqualTo(2);
    assertThat(PinDetector.pieceValue('P')).isEqualTo(1);
    assertThat(PinDetector.pieceValue('k')).isEqualTo(-6);
    assertThat(PinDetector.pieceValue('q')).isEqualTo(-5);
    assertThat(PinDetector.pieceValue('r')).isEqualTo(-4);
    assertThat(PinDetector.pieceValue('b')).isEqualTo(-3);
    assertThat(PinDetector.pieceValue('n')).isEqualTo(-2);
    assertThat(PinDetector.pieceValue('p')).isEqualTo(-1);
    assertThat(PinDetector.pieceValue('x')).isEqualTo(0);
  }
}
