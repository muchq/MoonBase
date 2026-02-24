package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class CheckmateDetectorTest {

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final CheckmateDetector detector = new CheckmateDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.CHECKMATE);
  }

  @Test
  public void checkmate_detectsMovesEndingWithHash() {
    List<PositionContext> positions = List.of(new PositionContext(30, SOME_FEN, true, "Qh7#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(30);
  }

  @Test
  public void checkmate_detectsPromotionCheckmate() {
    // Promotion that also gives checkmate
    List<PositionContext> positions = List.of(new PositionContext(45, SOME_FEN, true, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void checkmate_doesNotDetectCheck() {
    List<PositionContext> positions = List.of(new PositionContext(10, SOME_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_doesNotDetectQuietMove() {
    List<PositionContext> positions = List.of(new PositionContext(3, SOME_FEN, true, "Nf3"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
