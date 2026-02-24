package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class PromotionWithCheckmateDetectorTest {

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final PromotionWithCheckmateDetector detector = new PromotionWithCheckmateDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.PROMOTION_WITH_CHECKMATE);
  }

  @Test
  public void promotionWithCheckmate_detectsPromotionEndingWithHash() {
    List<PositionContext> positions = List.of(new PositionContext(45, SOME_FEN, false, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(45);
  }

  @Test
  public void promotionWithCheckmate_detectsCapturingPromotionWithCheckmate() {
    List<PositionContext> positions = List.of(new PositionContext(45, SOME_FEN, false, "dxe8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotionWithCheckmate_doesNotDetectQuietPromotion() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheckmate_doesNotDetectPromotionWithCheck() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheckmate_doesNotDetectRegularCheckmate() {
    List<PositionContext> positions = List.of(new PositionContext(30, SOME_FEN, false, "Qh7#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheckmate_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheckmate_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
