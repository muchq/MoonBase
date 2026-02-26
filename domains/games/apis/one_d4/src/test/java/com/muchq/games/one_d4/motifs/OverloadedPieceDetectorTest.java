package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class OverloadedPieceDetectorTest {

  // White queen at d5 (row 3, col 3) attacks three black rooks:
  //   e6 (row 2, col 4) — diagonal up-right ✓
  //   e5 (row 3, col 4) — same rank ✓
  //   e4 (row 4, col 4) — diagonal down-right ✓
  // Black rook at e5 defends both e6 (adjacent up) and e4 (adjacent down) — overloaded.
  // White just moved (Qd5), so black is to move → attackerIsWhite=true, defenderIsWhite=false.
  private static final String OVERLOADED_FEN = "4k3/8/4r3/3Qr3/4r3/8/8/4K3 b - - 0 1";

  // White queen attacks only one black piece — no overload possible.
  private static final String NOT_OVERLOADED_FEN = "4k3/8/8/3Qr3/8/8/8/4K3 b - - 0 1";

  private static final String SOME_FEN = "8/8/8/8/8/8/8/4K2k w - - 0 1";

  private final OverloadedPieceDetector detector = new OverloadedPieceDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.OVERLOADED_PIECE);
  }

  @Test
  public void overloadedPiece_detectsDefenderCoveringTwoAttackedSquares() {
    List<PositionContext> positions =
        List.of(new PositionContext(25, OVERLOADED_FEN, false, "Qd5"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(25);
  }

  @Test
  public void overloadedPiece_doesNotDetectWhenOnlyOnePieceAttacked() {
    List<PositionContext> positions =
        List.of(new PositionContext(25, NOT_OVERLOADED_FEN, false, "Qd5"));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void overloadedPiece_ignoresNullLastMove() {
    List<PositionContext> positions =
        List.of(new PositionContext(0, SOME_FEN, true, null));

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
    assertThat(occurrences).isEmpty();
  }

  @Test
  public void overloadedPiece_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
