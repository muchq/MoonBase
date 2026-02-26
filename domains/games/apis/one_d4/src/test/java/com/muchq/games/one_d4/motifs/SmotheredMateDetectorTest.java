package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class SmotheredMateDetectorTest {

  // Classic smothered mate:
  // Black king at h8 (row 0, col 7), black rook at g8 (row 0, col 6),
  // black pawns at g7 (row 1, col 6) and h7 (row 1, col 7).
  // White knight at g6 (row 2, col 6) delivers mate:
  //   knight (2,6) attacks (0,7) — L-shape: dr=2, dc=1 ✓
  // All 3 on-board adjacent squares of h8 are own (black) pieces.
  private static final String SMOTHERED_FEN = "6rk/6pp/6N1/8/8/8/8/6K1 b - - 0 1";

  // King on back rank, checkmated by rook (not knight) — not smothered mate.
  private static final String NOT_BY_KNIGHT_FEN = "R5k1/6pp/8/8/8/8/8/6K1 b - - 0 1";

  // Knight checks king at g8 (row 0, col 6) but adjacent square h8 is empty — not smothered.
  // Knight at f6 (row 2, col 5) attacks g8: (2-2,5+1)=(0,6) ✓
  private static final String NOT_SMOTHERED_FEN = "6k1/6p1/5N2/8/8/8/8/6K1 b - - 0 1";

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final SmotheredMateDetector detector = new SmotheredMateDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.SMOTHERED_MATE);
  }

  @Test
  public void smotheredMate_detectsKnightMateWithSmotheredKing() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, SMOTHERED_FEN, false, "Ng6#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(30);
  }

  @Test
  public void smotheredMate_doesNotDetectWhenMatingPieceIsNotKnight() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, NOT_BY_KNIGHT_FEN, false, "Ra8#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void smotheredMate_doesNotDetectWhenKingHasAdjacentEmptySquare() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, NOT_SMOTHERED_FEN, false, "Nf6#"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void smotheredMate_doesNotDetectNonCheckmateMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(30, SMOTHERED_FEN, false, "Ng6+"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void smotheredMate_ignoresNullLastMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void smotheredMate_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
