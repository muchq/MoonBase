package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class CheckDetectorTest {

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final CheckDetector detector = new CheckDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.CHECK);
  }

  @Test
  public void check_detectsMovesEndingWithPlus() {
    List<PositionContext> positions = List.of(new PositionContext(5, SOME_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(5);
  }

  @Test
  public void check_alsoDetectsCheckmate() {
    // Checkmate is a special case of check
    List<PositionContext> positions = List.of(new PositionContext(10, SOME_FEN, false, "Qh7#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void check_doesNotDetectQuietMove() {
    List<PositionContext> positions = List.of(new PositionContext(3, SOME_FEN, true, "Nf3"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void check_detectsPromotionWithCheck() {
    List<PositionContext> positions = List.of(new PositionContext(40, SOME_FEN, false, "e8=Q+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void check_detectsMultipleChecks() {
    List<PositionContext> positions =
        List.of(
            new PositionContext(5, SOME_FEN, false, "Rd8+"),
            new PositionContext(6, SOME_FEN, true, "Ke7"),
            new PositionContext(7, SOME_FEN, false, "Rb8#")); // checkmate is also a check

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(2);
  }

  @Test
  public void check_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void check_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
