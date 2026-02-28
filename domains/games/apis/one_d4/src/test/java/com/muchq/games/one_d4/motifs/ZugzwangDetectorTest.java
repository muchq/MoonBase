package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class ZugzwangDetectorTest {

  // Endgame: white king + white pawn (blocked) + black pawn + black king = 4 pieces, no queens.
  // White pawn at e4 (row 4, col 4) — black pawn directly in front at e5 (row 3, col 4) blocks it.
  // White has no non-king pieces that can reach empty squares (only king and 1 blocked pawn).
  // White to move (whiteToMove=true) → heuristic fires.
  private static final String ZUGZWANG_FEN = "4k3/8/8/4p3/4P3/8/8/4K3 w - - 0 1";

  // Same structure but white pawn is NOT blocked (e4, no black pawn in front at e5).
  private static final String PAWN_NOT_BLOCKED_FEN = "4k3/8/8/8/4P3/8/8/4K3 w - - 0 1";

  // Midgame with queens — not an endgame.
  private static final String QUEENS_PRESENT_FEN = "4k3/4q3/8/4p3/4P3/8/4Q3/4K3 w - - 0 1";

  // More than 8 pieces — not an endgame by the heuristic.
  private static final String TOO_MANY_PIECES_FEN =
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w - - 0 1";

  private final ZugzwangDetector detector = new ZugzwangDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.ZUGZWANG);
  }

  @Test
  public void zugzwang_detectsEndgameWithAllPawnsBlocked() {
    List<PositionContext> positions = List.of(new PositionContext(50, ZUGZWANG_FEN, true, "Ke2"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(50);
  }

  @Test
  public void zugzwang_doesNotDetectWhenPawnCanAdvance() {
    List<PositionContext> positions =
        List.of(new PositionContext(50, PAWN_NOT_BLOCKED_FEN, true, "Ke2"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void zugzwang_doesNotDetectWhenQueensPresent() {
    List<PositionContext> positions =
        List.of(new PositionContext(50, QUEENS_PRESENT_FEN, true, "Ke2"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void zugzwang_doesNotDetectWithTooManyPieces() {
    List<PositionContext> positions =
        List.of(new PositionContext(1, TOO_MANY_PIECES_FEN, true, "e4"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void zugzwang_ignoresNullLastMove() {
    List<PositionContext> positions = List.of(new PositionContext(0, ZUGZWANG_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void zugzwang_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
