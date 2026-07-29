package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.Motif;
import java.util.List;
import org.junit.jupiter.api.Test;

public class DetectorsTest {

  @Test
  public void defaultDetectors_coverTheExpectedMotifs() {
    List<MotifDetector> detectors = Detectors.defaultDetectors();

    assertThat(detectors).extracting(MotifDetector::motif).doesNotHaveDuplicates();
    assertThat(detectors)
        .extracting(MotifDetector::motif)
        .containsExactlyInAnyOrder(
            Motif.PIN,
            Motif.CROSS_PIN,
            Motif.SKEWER,
            Motif.ATTACK,
            Motif.CHECK,
            Motif.PROMOTION,
            Motif.PROMOTION_WITH_CHECK,
            Motif.PROMOTION_WITH_CHECKMATE,
            Motif.BACK_RANK_MATE,
            Motif.SMOTHERED_MATE);
  }
}
