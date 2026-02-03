package com.muchq.one_d4.motifs;

import com.muchq.one_d4.engine.model.GameFeatures;
import com.muchq.one_d4.engine.model.Motif;
import com.muchq.one_d4.engine.model.PositionContext;
import java.util.List;

public interface MotifDetector {
  Motif motif();

  List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions);
}
