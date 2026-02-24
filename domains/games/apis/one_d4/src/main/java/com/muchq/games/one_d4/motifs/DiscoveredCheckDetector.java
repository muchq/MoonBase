package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

/**
 * Detects discovered checks: positions where a discovered attack also gives check (move ends with
 * '+' or '#') and the discovered-attack pattern is present.
 */
public class DiscoveredCheckDetector implements MotifDetector {

  private final DiscoveredAttackDetector discoveredAttackDetector = new DiscoveredAttackDetector();

  @Override
  public Motif motif() {
    return Motif.DISCOVERED_CHECK;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (int i = 1; i < positions.size(); i++) {
      PositionContext after = positions.get(i);
      String move = after.lastMove();
      if (move == null) continue;
      // Must be a check or checkmate
      if (!move.endsWith("+") && !move.endsWith("#")) continue;

      PositionContext before = positions.get(i - 1);
      String beforePlacement = before.fen().split(" ")[0];
      String afterPlacement = after.fen().split(" ")[0];
      int[][] boardBefore = PinDetector.parsePlacement(beforePlacement);
      int[][] boardAfter = PinDetector.parsePlacement(afterPlacement);
      boolean moverIsWhite = !after.whiteToMove();

      if (discoveredAttackDetector.hasDiscoveredAttackPublic(
          boardBefore, boardAfter, moverIsWhite)) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(
                after, "Discovered check at move " + after.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }
}
