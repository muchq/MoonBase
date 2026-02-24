package com.muchq.games.one_d4.engine.model;

import java.util.List;
import java.util.Map;
import java.util.Set;

public record GameFeatures(
    Set<Motif> motifs, int numMoves, Map<Motif, List<MotifOccurrence>> occurrences) {
  public boolean hasMotif(Motif motif) {
    return motifs.contains(motif);
  }

  public record MotifOccurrence(int ply, int moveNumber, String side, String description) {
    /**
     * Factory: derives ply and side from the given PositionContext. Returns null if the context
     * represents the initial position (no move has been made).
     */
    public static MotifOccurrence from(PositionContext ctx, String description) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null; // initial position, no move made yet
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(ply, ctx.moveNumber(), side, description);
    }
  }
}
