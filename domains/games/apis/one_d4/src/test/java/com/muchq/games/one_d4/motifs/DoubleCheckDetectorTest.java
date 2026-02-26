package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class DoubleCheckDetectorTest {

  // Black king at a8 (row 0, col 0).
  // White rook at a1 (row 7, col 0) attacks via a-file (clear path: rows 1-6 col 0 all empty).
  // White bishop at d5 (row 3, col 3) attacks a8 diagonally: (3,3)→(2,2)→(1,1)→(0,0) — all clear.
  // Two attackers on black king → double check.
  // Black to move (white just delivered double check).
  private static final String DOUBLE_CHECK_FEN = "k7/8/8/3B4/8/8/8/R6K b - - 0 1";

  // Black king at a8, only white bishop attacks (rook at b1 — different file, not on a-file).
  private static final String SINGLE_CHECK_FEN = "k7/8/8/3B4/8/8/8/1R5K b - - 0 1";

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final DoubleCheckDetector detector = new DoubleCheckDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.DOUBLE_CHECK);
  }

  @Test
  public void doubleCheck_detectsTwoPiecesAttackingKing() {
    List<PositionContext> positions =
        List.of(new PositionContext(25, DOUBLE_CHECK_FEN, false, "Bd5+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(25);
  }

  @Test
  public void doubleCheck_doesNotDetectSingleCheck() {
    List<PositionContext> positions =
        List.of(new PositionContext(25, SINGLE_CHECK_FEN, false, "Bd5+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void doubleCheck_doesNotDetectQuietMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(25, DOUBLE_CHECK_FEN, false, "Bd5"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void doubleCheck_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void doubleCheck_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
