package com.muchq.games.one_d4.engine.model;

import java.util.List;
import java.util.Map;
import java.util.Set;
import org.jspecify.annotations.Nullable;

public record GameFeatures(
    Set<Motif> motifs, int numMoves, Map<Motif, List<MotifOccurrence>> occurrences) {
  public boolean hasMotif(Motif motif) {
    return motifs.contains(motif);
  }

  public record MotifOccurrence(
      int ply,
      int moveNumber,
      String side,
      String description,
      @Nullable String movedPiece,
      @Nullable String attacker,
      @Nullable String target,
      boolean isDiscovered,
      boolean isMate,
      @Nullable String pinType) {

    /**
     * Factory for ATTACK motif occurrences. {@code isDiscovered} is true when the piece that moved
     * differs from the attacking piece (a sliding piece was revealed). {@code isMate} is true when
     * the attack delivers checkmate.
     */
    public static MotifOccurrence attack(
        int ply,
        int moveNumber,
        String side,
        String description,
        String movedPiece,
        String attacker,
        String target,
        boolean isDiscovered,
        boolean isMate) {
      return new MotifOccurrence(
          ply,
          moveNumber,
          side,
          description,
          movedPiece,
          attacker,
          target,
          isDiscovered,
          isMate,
          null);
    }
  }
}
