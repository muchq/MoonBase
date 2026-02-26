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
      @Nullable String target) {
    /**
     * Factory: derives ply and side from the given PositionContext. Returns null if the context
     * represents the initial position (no move has been made).
     */
    public static MotifOccurrence from(PositionContext ctx, String description) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null; // initial position, no move made yet
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(ply, ctx.moveNumber(), side, description, null, null, null);
    }

    /** Factory for discovered attack/check occurrences with structured piece data. */
    public static MotifOccurrence discoveredAttack(
        PositionContext ctx,
        String description,
        String movedPiece,
        String attacker,
        String target) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null;
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(ply, ctx.moveNumber(), side, description, movedPiece, attacker, target);
    }
  }
}
