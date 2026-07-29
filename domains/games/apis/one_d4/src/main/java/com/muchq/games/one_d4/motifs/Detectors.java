package com.muchq.games.one_d4.motifs;

import java.util.List;

/**
 * The canonical detector set used when indexing games. Both the one_d4 service (IndexerModule) and
 * the mcpserver's in-process indexer (McpModule) assemble their FeatureExtractor from this list —
 * keep it the single definition, or indexes built by the two processes silently disagree on which
 * motifs exist.
 */
public final class Detectors {

  private Detectors() {}

  public static List<MotifDetector> defaultDetectors() {
    return List.of(
        new PinDetector(),
        new CrossPinDetector(),
        new SkewerDetector(),
        new AttackDetector(),
        new CheckDetector(),
        new PromotionDetector(),
        new PromotionWithCheckDetector(),
        new PromotionWithCheckmateDetector(),
        new BackRankMateDetector(),
        new SmotheredMateDetector());
  }
}
