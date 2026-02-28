package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class SkewerDetectorTest {

  private final SkewerDetector detector = new SkewerDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.SKEWER);
  }

  // === Skewer cases ===

  @Test
  public void skewer_rookSkewersKingAndRook() {
    // White rook on a4 attacks black king on e4, with black rook on h4 behind
    String fen = "8/8/8/8/R3k2r/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(15, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.attacker()).isEqualTo("Ra4");
    assertThat(occ.target()).isEqualTo("ke4");
    assertThat(occ.pinType()).isNull();
    assertThat(occ.isDiscovered()).isFalse();
    assertThat(occ.isMate()).isFalse();
  }

  @Test
  public void skewer_queenSkewersKingAndBishop() {
    // White queen on a1 attacks black king on d4, with black bishop on g7 behind
    String fen = "8/6b1/8/8/3k4/8/8/Q3K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(20, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isEqualTo("Qa1");
    assertThat(occurrences.get(0).target()).isEqualTo("kd4");
  }

  @Test
  public void skewer_bishopSkewersQueenAndRook() {
    // White bishop on b1 attacks black queen on d3, with black rook on f5 behind
    String fen = "8/8/8/5r2/8/3q4/8/1B2K2k b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(18, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isEqualTo("Bb1");
    assertThat(occurrences.get(0).target()).isEqualTo("qd3");
  }

  @Test
  public void skewer_rookSkewersQueenAndKnight() {
    // White rook on a5 attacks black queen on d5, with black knight on h5 behind
    String fen = "8/8/8/R2q3n/8/8/8/4K2k b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(22, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isEqualTo("Ra5");
    assertThat(occurrences.get(0).target()).isEqualTo("qd5");
  }

  @Test
  public void skewer_blackSkewersWhitePieces() {
    // Black rook on h4 attacks white king on e4, with white bishop on b4 behind
    String fen = "8/8/8/8/1B2K2r/8/8/7k w - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(15, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).side()).isEqualTo("black");
    assertThat(occurrences.get(0).attacker()).isEqualTo("rh4");
    assertThat(occurrences.get(0).target()).isEqualTo("Ke4");
  }

  // === Not a skewer cases ===

  @Test
  public void notSkewer_lessValuableInFront() {
    // This is a PIN, not a skewer: knight in front, king behind
    String fen = "8/8/8/8/R3n2k/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_sameValuePieces() {
    // Rook attacks rook with rook behind - same value, not a skewer
    String fen = "8/8/8/8/R3r2r/8/8/4K2k b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_onlyOnePieceOnRay() {
    // Rook attacks king, but nothing behind
    String fen = "8/8/8/8/R3k3/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_pawnBehind() {
    // King skewered to pawn - but pawn value (1) < 2, not counted
    String fen = "8/8/8/8/R3k2p/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_friendlyPieceBlocks() {
    // White rook on a4, white knight on c4 blocks any skewer potential
    String fen = "8/8/8/8/R1N1k2r/8/8/4K3 b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_knightCannotSkewer() {
    // Knights cannot create skewers (don't slide)
    String fen = "8/8/5q2/8/4N3/8/3r4/4K2k b - - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(10, fen, false, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_startingPosition() {
    String fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    List<PositionContext> positions = List.of(new PositionContext(1, fen, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void notSkewer_emptyList() {
    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of());
    assertThat(occurrences).isEmpty();
  }
}
