package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.ArrayList;
import java.util.List;

public class PromotionDetector implements MotifDetector {

  @Override
  public Motif motif() {
    return Motif.PROMOTION;
  }

  @Override
  public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
    List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

    for (PositionContext ctx : positions) {
      String move = ctx.lastMove();
      if (move != null && move.contains("=")) {
        occurrences.add(
            new GameFeatures.MotifOccurrence(
                ctx.moveNumber(), "Promotion at move " + ctx.moveNumber()));
      }
    }

    return occurrences;
  }
}
