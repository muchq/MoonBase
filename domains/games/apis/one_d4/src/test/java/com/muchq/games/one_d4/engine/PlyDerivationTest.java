package com.muchq.games.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import com.muchq.games.one_d4.motifs.Detectors;
import org.junit.jupiter.api.Test;

/**
 * Ply is the half-move index: White's moves odd, Black's even, both colors of one move sharing a
 * move number.
 *
 * <p>It was wrong for Black for as long as motif_occurrences has existed — six copies of {@code 2 *
 * (moveNumber - 1)}, which is two plies early and drops Black's first move entirely. Nothing here
 * asserted it, which is why porting the pipeline to C++ (#1389 phase 2) is what found it. The
 * derivation is one method now; this is the test it should have had.
 */
public class PlyDerivationTest {

  private static PositionContext after(int moveNumber, boolean whiteToMove) {
    return new PositionContext(moveNumber, "", whiteToMove, "e4");
  }

  @Test
  public void whiteMovesAreOddPlies() {
    // whiteToMove == false means White has just moved.
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(1, false))).isEqualTo(1);
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(2, false))).isEqualTo(3);
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(20, false))).isEqualTo(39);
  }

  @Test
  public void blackMovesAreEvenPlies() {
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(1, true))).isEqualTo(2);
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(2, true))).isEqualTo(4);
    assertThat(GameFeatures.MotifOccurrence.plyOf(after(20, true))).isEqualTo(40);
  }

  @Test
  public void theInitialPositionIsNotAMove() {
    assertThat(GameFeatures.MotifOccurrence.plyOf(new PositionContext(0, "", true, null)))
        .isEqualTo(0);
  }

  @Test
  public void blacksFirstMoveIsRecorded() {
    // The shape of the old bug: 1... came out as ply 0 and every factory
    // discarded it as "the initial position".
    String pgn =
        "[Event \"x\"]\n\n"
            + "1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4 4. Nxe5 Qg5 5. Nxf7 Qxg2 6. Rf1 Qxe4+ "
            + "7. Be2 Nf3# 0-1\n";
    GameFeatures features =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors())
            .extractOrThrow(pgn);

    for (Motif motif : features.occurrences().keySet()) {
      for (GameFeatures.MotifOccurrence occ : features.occurrences().get(motif)) {
        boolean white = "white".equals(occ.side());
        assertThat(occ.ply())
            .as("%s at move %d by %s", motif, occ.moveNumber(), occ.side())
            .isEqualTo(white ? 2 * occ.moveNumber() - 1 : 2 * occ.moveNumber());
        assertThat(occ.ply() % 2 == 1).as("parity follows color").isEqualTo(white);
      }
    }
  }
}
