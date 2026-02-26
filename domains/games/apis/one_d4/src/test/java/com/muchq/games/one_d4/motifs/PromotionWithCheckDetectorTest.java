package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class PromotionWithCheckDetectorTest {

  // White queen at e8 (row 0, col 4) checks black king at e7 (row 1, col 4)
  // via the same-column queen attack. White just promoted, so black is to move.
  private static final String QUEEN_CHECKS_FEN = "4Q3/4k3/8/8/8/8/8/7K b - - 0 1";

  // White rook at e8 (row 0, col 4) checks black king at a8 (row 0, col 0)
  // via the same-rank rook attack with clear path.
  private static final String ROOK_CHECKS_FEN = "k3R3/8/8/8/8/8/8/7K b - - 0 1";

  // White queen at e8 (row 0, col 4), black rook at c8 (row 0, col 2) blocks path to king at a8.
  // The promoted queen does NOT directly attack the king — discovered check scenario.
  private static final String DISCOVERED_CHECK_FEN = "k1r1Q3/8/8/8/8/8/8/7K b - - 0 1";

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final PromotionWithCheckDetector detector = new PromotionWithCheckDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.PROMOTION_WITH_CHECK);
  }

  @Test
  public void promotionWithCheck_detectsWhenPromotedPieceAttacksKing() {
    List<PositionContext> positions =
        List.of(new PositionContext(40, QUEEN_CHECKS_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(40);
  }

  @Test
  public void promotionWithCheck_detectsCapturingPromotionWithCheck() {
    List<PositionContext> positions =
        List.of(new PositionContext(40, ROOK_CHECKS_FEN, false, "dxe8=R+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void promotionWithCheck_doesNotDetectWhenPromotedPieceBlockedFromKing() {
    // Promoted queen at e8 is blocked by own/enemy piece from reaching the king.
    List<PositionContext> positions =
        List.of(new PositionContext(40, DISCOVERED_CHECK_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_doesNotDetectQuietPromotion() {
    List<PositionContext> positions =
        List.of(new PositionContext(38, QUEEN_CHECKS_FEN, false, "e8=Q"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_doesNotDetectPromotionWithCheckmate() {
    List<PositionContext> positions =
        List.of(new PositionContext(38, QUEEN_CHECKS_FEN, false, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_doesNotDetectRegularCheck() {
    List<PositionContext> positions =
        List.of(new PositionContext(10, SOME_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_ignoresNullLastMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void promotionWithCheck_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
