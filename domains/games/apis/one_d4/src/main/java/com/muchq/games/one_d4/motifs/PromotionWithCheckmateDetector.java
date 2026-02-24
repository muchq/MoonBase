package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class PromotionWithCheckmateDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.PROMOTION_WITH_CHECKMATE;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move != null && move.contains("=") && move.endsWith("#")) {
        GameFeatures.MotifOccurrence occ =
            GameFeatures.MotifOccurrence.from(
                ctx, "Promotion with checkmate at move " + ctx.moveNumber());
        if (occ != null) occurrences.add(occ);
      }
    }

    return occurrences;
  }
}
