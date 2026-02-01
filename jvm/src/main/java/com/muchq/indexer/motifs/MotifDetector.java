package com.muchq.indexer.motifs;

import com.muchq.indexer.engine.model.GameFeatures;
import com.muchq.indexer.engine.model.Motif;
import com.muchq.indexer.engine.model.PositionContext;

import java.util.List;

public interface MotifDetector {
    Motif motif();
    List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions);
}
