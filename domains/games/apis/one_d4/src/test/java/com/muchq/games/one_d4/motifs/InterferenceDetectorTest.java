package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class InterferenceDetectorTest {

  // White knight moves from c3 (row 5, col 2) to a4 (row 4, col 0).
  // Before: a4 is empty; black rook at a1 (row 7, col 0) has clear line through a4 to a8.
  // After: white knight occupies a4, blocking the rook's upward attack line.
  private static final String BEFORE_FEN = "4k3/8/8/8/8/2N5/8/r3K3 w - - 0 1";
  private static final String AFTER_FEN = "4k3/8/8/8/N7/8/8/r3K3 b - - 0 1";

  // White knight moves to d4 (row 4, col 3) which is NOT on any enemy sliding piece's attack line.
  // Black rook at a1 is on a-file; d4 is on d-file — no interference.
  private static final String NOT_INTERFERING_AFTER_FEN = "4k3/8/8/8/3N4/8/8/r3K3 b - - 0 1";
  private static final String NOT_INTERFERING_BEFORE_FEN = "4k3/8/8/8/8/3N4/8/r3K3 w - - 0 1";

  private final InterferenceDetector detector = new InterferenceDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.INTERFERENCE);
  }

  @Test
  public void interference_detectsPieceBlockingEnemySlidingLine() {
    PositionContext before = new PositionContext(20, BEFORE_FEN, true, "Nc3");
    PositionContext after = new PositionContext(21, AFTER_FEN, false, "Na4");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(21);
  }

  @Test
  public void interference_doesNotDetectWhenDestNotOnSlidingLine() {
    PositionContext before = new PositionContext(20, NOT_INTERFERING_BEFORE_FEN, true, "Nd3");
    PositionContext after = new PositionContext(21, NOT_INTERFERING_AFTER_FEN, false, "Nd4");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void interference_requiresAtLeastTwoPositions() {
    PositionContext single = new PositionContext(20, BEFORE_FEN, true, "Nc3");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(single));
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void interference_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
