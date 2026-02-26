package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class BackRankMateDetectorTest {

  // Black king at g8 (row 0, col 6), white rook at a8 (row 0, col 0) delivers mate.
  // Black pawns at g7 (row 1, col 6) and h7 (row 1, col 7) block escape rank.
  // White just moved (Ra8#), so black is to move — loserIsWhite=false, backRankRow=0.
  private static final String BLACK_BACK_RANK_FEN = "R5k1/6pp/8/8/8/8/8/6K1 b - - 0 1";

  // White king at g1 (row 7, col 6), black rook at a1 (row 7, col 0) delivers mate.
  // White pawns at g2 (row 6, col 6) and h2 (row 6, col 7) block escape rank.
  // Black just moved (Ra1#), so white is to move — loserIsWhite=true, backRankRow=7.
  private static final String WHITE_BACK_RANK_FEN = "6k1/8/8/8/8/8/6PP/r5K1 w - - 0 1";

  // King not on back rank — king at g7 (row 1, col 6) instead.
  private static final String NOT_BACK_RANK_FEN = "8/R5kp/7p/8/8/8/8/6K1 b - - 0 1";

  // King on back rank but escape rank not blocked by own pieces.
  private static final String NOT_BLOCKED_FEN = "R6k/8/8/8/8/8/8/6K1 b - - 0 1";

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final BackRankMateDetector detector = new BackRankMateDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.BACK_RANK_MATE);
  }

  @Test
  public void backRankMate_detectsBlackKingMatedOnRank8() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, BLACK_BACK_RANK_FEN, false, "Ra8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(30);
  }

  @Test
  public void backRankMate_detectsWhiteKingMatedOnRank1() {
    List<PositionContext> positions =
        List.of(new PositionContext(31, WHITE_BACK_RANK_FEN, true, "Ra1#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void backRankMate_doesNotDetectKingNotOnBackRank() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, NOT_BACK_RANK_FEN, false, "Ra7#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void backRankMate_doesNotDetectWhenEscapeRankNotBlocked() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, NOT_BLOCKED_FEN, false, "Ra8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void backRankMate_doesNotDetectNonCheckmateMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, BLACK_BACK_RANK_FEN, false, "Ra8+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void backRankMate_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void backRankMate_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
