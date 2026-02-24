package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class PromotionDetectorTest {

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final PromotionDetector detector = new PromotionDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.PROMOTION);
  }

  @Test
  public void promotion_detectsQuietPromotion() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(38);
  }

  @Test
  public void promotion_detectsPromotionToRook() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "a8=R"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotion_alsoDetectsPromotionWithCheck() {
    // Promotion with check is still a promotion
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotion_alsoDetectsPromotionWithCheckmate() {
    // Promotion with checkmate is still a promotion
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotion_alsoDetectsCapturingPromotionWithCheck() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "dxe8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotion_detectsCapturingPromotion() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "dxe8=Q"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotion_doesNotDetectRegularMove() {
    List<PositionContext> positions = List.of(new PositionContext(5, SOME_FEN, true, "e4"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotion_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotion_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
