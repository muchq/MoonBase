package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class SacrificeDetectorTest {

  // White queen (value=5) captures black pawn (value=1) at e4.
  // Before: white queen at e5 (row 3, col 4), black pawn at e4 (row 4, col 4).
  // After: white queen at e4.
  private static final String BEFORE_Q_TAKES_P =
      "4k3/8/8/4Q3/4p3/8/8/4K3 w - - 0 1";
  private static final String AFTER_Q_TAKES_P =
      "4k3/8/8/8/4Q3/8/8/4K3 b - - 0 1";

  // White rook (value=4) captures black rook (value=4) at e5 — equal trade, not a sacrifice.
  private static final String BEFORE_R_TAKES_R =
      "4k3/8/8/4r3/4R3/8/8/4K3 w - - 0 1";
  private static final String AFTER_R_TAKES_R =
      "4k3/8/8/4R3/8/8/8/4K3 b - - 0 1";

  // Pawn (value=1) captures queen (value=5) — pawn gains material, not a sacrifice.
  private static final String BEFORE_P_TAKES_Q =
      "4k3/8/8/4q3/5P2/8/8/4K3 w - - 0 1";
  private static final String AFTER_P_TAKES_Q =
      "4k3/8/8/4P3/8/8/8/4K3 b - - 0 1";

  private final SacrificeDetector detector = new SacrificeDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.SACRIFICE);
  }

  @Test
  public void sacrifice_detectsHigherValuePieceCapturingLowerValue() {
    PositionContext before = new PositionContext(20, BEFORE_Q_TAKES_P, true, "Qe5");
    PositionContext after = new PositionContext(20, AFTER_Q_TAKES_P, false, "Qxe4");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(20);
  }

  @Test
  public void sacrifice_doesNotDetectEqualTrade() {
    PositionContext before = new PositionContext(20, BEFORE_R_TAKES_R, true, "Re4");
    PositionContext after = new PositionContext(20, AFTER_R_TAKES_R, false, "Rxe5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void sacrifice_doesNotDetectWhenCaptureGainsMaterial() {
    PositionContext before = new PositionContext(20, BEFORE_P_TAKES_Q, true, "Pf4");
    PositionContext after = new PositionContext(20, AFTER_P_TAKES_Q, false, "fxe5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void sacrifice_requiresAtLeastTwoPositions() {
    PositionContext single = new PositionContext(20, BEFORE_Q_TAKES_P, true, "Qe5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(single));
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void sacrifice_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
