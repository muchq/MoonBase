package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class CheckmateDetectorTest {

  // White rook on d8, black king on h8 (checkmated in this test position)
  private static final String MATE_FEN = "3R3k/8/8/8/8/8/8/4K3 b - - 0 30";
  // Black queen on h4 mates white king on e1
  private static final String BLACK_MATE_FEN = "7k/8/8/8/7q/8/8/4K3 w - - 0 20";

  private final CheckmateDetector detector = new CheckmateDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.CHECKMATE);
  }

  @Test
  public void checkmate_detectsMovesEndingWithHash() {
    List<PositionContext> positions = List.of(new PositionContext(30, MATE_FEN, false, "Rd8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(30);
    assertThat(occurrences.get(0).side()).isEqualTo("white");
  }

  @Test
  public void checkmate_populatesAttackerTargetAndIsMate() {
    // MATE_FEN: white rook on d8 (row=0, col=3), black king on h8 (row=0, col=7)
    List<PositionContext> positions = List.of(new PositionContext(30, MATE_FEN, false, "Rd8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.attacker()).isEqualTo("Rd8");
    assertThat(occ.target()).isEqualTo("kh8");
    assertThat(occ.isMate()).isTrue();
    assertThat(occ.isDiscovered()).isFalse();
    assertThat(occ.pinType()).isNull();
  }

  @Test
  public void checkmate_blackDeliversCheckmate() {
    // BLACK_MATE_FEN: black queen on h4, white king on e1
    List<PositionContext> positions =
        List.of(new PositionContext(20, BLACK_MATE_FEN, true, "Qh4#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).side()).isEqualTo("black");
    assertThat(occurrences.get(0).attacker()).isEqualTo("qh4");
    assertThat(occurrences.get(0).target()).isEqualTo("Ke1");
    assertThat(occurrences.get(0).isMate()).isTrue();
  }

  @Test
  public void checkmate_detectsPromotionCheckmate() {
    // Promotion that also gives checkmate — queen on e8 with black king on f8
    String promotionFen = "4Qk2/8/8/8/8/8/8/4K3 b - - 0 45";
    List<PositionContext> positions =
        List.of(new PositionContext(45, promotionFen, false, "e8=Q#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).isMate()).isTrue();
  }

  @Test
  public void checkmate_doesNotDetectCheck() {
    List<PositionContext> positions = List.of(new PositionContext(10, MATE_FEN, false, "Rd8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_doesNotDetectQuietMove() {
    List<PositionContext> positions = List.of(new PositionContext(3, MATE_FEN, true, "Nf3"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, MATE_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void checkmate_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
