package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class CheckDetectorTest {

  // White rook on d8 gives check to black king on h8 (same rank, clear path).
  // White king e1, black king h8.
  private static final String CHECK_FEN = "3R3k/8/8/8/8/8/8/4K3 b - - 0 5";
  // Black queen h4 checks white king e1: "8/8/8/8/7q/8/8/4K3 w - - 0 1"
  private static final String BLACK_CHECK_FEN = "7k/8/8/8/7q/8/8/4K3 w - - 0 10";

  private final CheckDetector detector = new CheckDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.CHECK);
  }

  @Test
  public void check_detectsMovesEndingWithPlus() {
    List<PositionContext> positions = List.of(new PositionContext(5, CHECK_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(5);
    assertThat(occurrences.get(0).side()).isEqualTo("white");
  }

  @Test
  public void check_populatesAttackerAndTarget() {
    // CHECK_FEN: white rook on d8 (row=0, col=3) attacks black king on h8 (row=0, col=7)
    List<PositionContext> positions = List.of(new PositionContext(5, CHECK_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    // Rook on d8 = attacker; black king on h8 = target
    assertThat(occ.attacker()).isEqualTo("Rd8");
    assertThat(occ.target()).isEqualTo("kh8");
    assertThat(occ.isMate()).isFalse();
    assertThat(occ.isDiscovered()).isFalse();
    assertThat(occ.pinType()).isNull();
  }

  @Test
  public void check_blackDeliversCheck() {
    // BLACK_CHECK_FEN: black queen on h4 checks white king on e1
    List<PositionContext> positions =
        List.of(new PositionContext(10, BLACK_CHECK_FEN, true, "Qh4+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).side()).isEqualTo("black");
    assertThat(occurrences.get(0).attacker()).isEqualTo("qh4");
    assertThat(occurrences.get(0).target()).isEqualTo("Ke1");
  }

  @Test
  public void check_alsoDetectsCheckmate() {
    // Checkmate is a special case of check
    List<PositionContext> positions = List.of(new PositionContext(10, CHECK_FEN, false, "Rd8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isNotNull();
    assertThat(occurrences.get(0).target()).isNotNull();
  }

  @Test
  public void check_doesNotDetectQuietMove() {
    List<PositionContext> positions = List.of(new PositionContext(3, CHECK_FEN, true, "Nf3"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void check_detectsPromotionWithCheck() {
    // Promotion square: e8 = row 0, col 4; promoted queen on e8 with black king on h8
    // FEN: 7k/8/8/8/8/8/8/4K3 with white queen on e8 after promotion
    String promotionFen = "4Qk2/8/8/8/8/8/8/4K3 b - - 0 40";
    List<PositionContext> positions =
        List.of(new PositionContext(40, promotionFen, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isEqualTo("Qe8");
    assertThat(occurrences.get(0).target()).isEqualTo("kf8");
  }

  @Test
  public void check_detectsMultipleChecks() {
    List<PositionContext> positions =
        List.of(
            new PositionContext(5, CHECK_FEN, false, "Rd8+"),
            new PositionContext(6, CHECK_FEN, true, "Ke7"),
            new PositionContext(7, CHECK_FEN, false, "Rb8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(2);
  }

  @Test
  public void check_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, CHECK_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void check_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
