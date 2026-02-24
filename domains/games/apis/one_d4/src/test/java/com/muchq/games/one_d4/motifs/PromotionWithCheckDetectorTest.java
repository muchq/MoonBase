package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class PromotionWithCheckDetectorTest {

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final PromotionWithCheckDetector detector = new PromotionWithCheckDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.PROMOTION_WITH_CHECK);
  }

  @Test
  public void promotionWithCheck_detectsPromotionEndingWithPlus() {
    List<PositionContext> positions = List.of(new PositionContext(40, SOME_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(40);
  }

  @Test
  public void promotionWithCheck_detectsCapturingPromotionWithCheck() {
    List<PositionContext> positions = List.of(new PositionContext(40, SOME_FEN, false, "dxe8=R+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotionWithCheck_doesNotDetectQuietPromotion() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_doesNotDetectPromotionWithCheckmate() {
    List<PositionContext> positions = List.of(new PositionContext(38, SOME_FEN, false, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_doesNotDetectRegularCheck() {
    List<PositionContext> positions = List.of(new PositionContext(10, SOME_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
