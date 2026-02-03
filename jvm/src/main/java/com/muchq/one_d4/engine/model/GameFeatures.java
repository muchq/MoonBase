package com.muchq.one_d4.engine.model;

import java.util.List;
import java.util.Map;
import java.util.Set;

public record GameFeatures(
    Set<Motif> motifs, int numMoves, Map<Motif, List<MotifOccurrence>> occurrences) {
  public boolean hasMotif(Motif motif) {
    return motifs.contains(motif);
  }

  public record MotifOccurrence(int moveNumber, String description) {}
}
