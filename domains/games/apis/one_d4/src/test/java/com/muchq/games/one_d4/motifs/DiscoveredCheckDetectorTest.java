package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class DiscoveredCheckDetectorTest {

  private final DiscoveredCheckDetector detector = new DiscoveredCheckDetector();

  // White rook on e1, white bishop on e4 blocking the e-file, black king on e8.
  // When the bishop moves off the e-file, the rook delivers a discovered check.
  private static final String BEFORE_ROOK_BLOCKS_E_FILE = "4k3/8/8/8/4B3/8/8/4R3 w - - 0 1";
  // Bishop has moved to h7; rook on e1 now has a clear line to king on e8.
  private static final String AFTER_ROOK_CHECKS_KING = "4k3/7B/8/8/8/8/8/4R3 b - - 1 1";

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.DISCOVERED_CHECK);
  }

  @Test
  public void discoveredCheck_firesWhenBishopUnblocksRookFromKing() {
    // White bishop on e4 moves to h7, revealing rook on e1 checking king on e8.
    PositionContext before = new PositionContext(10, BEFORE_ROOK_BLOCKS_E_FILE, true, null);
    PositionContext after = new PositionContext(10, AFTER_ROOK_CHECKS_KING, false, "Bh7+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.moveNumber()).isEqualTo(10);
    assertThat(occ.side()).isEqualTo("white");
    assertThat(occ.movedPiece()).isEqualTo("Be4->h7");
    assertThat(occ.attacker()).isEqualTo("Re1");
    assertThat(occ.target()).isEqualTo("ke8");
  }

  @Test
  public void discoveredCheck_alsoFiresForDiscoveredCheckmate() {
    // Same setup; # instead of + — discovered checkmate is still a discovered check.
    PositionContext before = new PositionContext(10, BEFORE_ROOK_BLOCKS_E_FILE, true, null);
    PositionContext after = new PositionContext(10, AFTER_ROOK_CHECKS_KING, false, "Bh7#");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
  }

  @Test
  public void discoveredCheck_firesWhenKnightUnblocksBishopFromKing() {
    // White bishop on a2, white knight on d5 sitting on the a2-f7 diagonal, black king on f7.
    // Knight moves from d5 to f4, clearing the diagonal so bishop checks king on f7.
    String beforeFen = "8/5k2/8/3N4/8/8/B7/4K3 w - - 0 1";
    String afterFen = "8/5k2/8/8/5N2/8/B7/4K3 b - - 1 1";
    PositionContext before = new PositionContext(8, beforeFen, true, null);
    PositionContext after = new PositionContext(8, afterFen, false, "Nf4+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).moveNumber()).isEqualTo(8);
  }

  @Test
  public void discoveredCheck_firesForBlackDiscoveredCheck() {
    // Black rook on e8, black bishop on e5 blocking the e-file, white king on e1.
    // Black bishop moves to h2, revealing rook on e8 checking white king on e1.
    String beforeFen = "4r3/8/8/4b3/8/8/8/4K3 b - - 0 1";
    String afterFen = "4r3/8/8/8/8/8/7b/4K3 w - - 1 1";
    PositionContext before = new PositionContext(10, beforeFen, false, null);
    PositionContext after = new PositionContext(10, afterFen, true, "Bh2+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).side()).isEqualTo("black");
  }

  @Test
  public void discoveredCheck_doesNotFireForRegularDirectCheck() {
    // White bishop on f3 moves to d5, directly checking black king on c6.
    // No sliding piece is revealed behind f3, so this is a plain check, not a discovered check.
    String beforeFen = "8/8/2k5/8/8/5B2/8/4K3 w - - 0 1";
    String afterFen = "8/8/2k5/3B4/8/8/8/4K3 b - - 1 1";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Bd5+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isEmpty();
  }

  @Test
  public void discoveredCheck_doesNotFireWhenNoCheckNotation() {
    // Same FEN transition as the rook test (discovered attack is present in the position),
    // but the move has no '+' — the detector requires both the pattern and the annotation.
    PositionContext before = new PositionContext(10, BEFORE_ROOK_BLOCKS_E_FILE, true, null);
    PositionContext after = new PositionContext(10, AFTER_ROOK_CHECKS_KING, false, "Bh7");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isEmpty();
  }

  @Test
  public void discoveredCheck_doesNotFireWithSinglePosition() {
    // Need at least two consecutive positions to compare boards.
    PositionContext p = new PositionContext(1, BEFORE_ROOK_BLOCKS_E_FILE, true, null);

    assertThat(detector.detect(List.of(p))).isEmpty();
  }

  @Test
  public void discoveredCheck_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
