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
     * Factory: derives ply and side from the given PositionContext. Returns null if the context
     * represents the initial position (no move has been made).
     */
    public static MotifOccurrence from(PositionContext ctx, String description) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null; // initial position, no move made yet
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(
          ply, ctx.moveNumber(), side, description, null, null, null, false, false, null);
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
      return new MotifOccurrence(
          ply,
          ctx.moveNumber(),
          side,
          description,
          movedPiece,
          attacker,
          target,
          false,
          false,
          null);
    }

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

    /**
     * Factory for motif occurrences with attacker/target but no movedPiece (e.g. check,
     * double check, overloaded piece).
     */
    public static MotifOccurrence withPiece(
        PositionContext ctx,
        String description,
        @Nullable String attacker,
        @Nullable String target) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null;
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(
          ply, ctx.moveNumber(), side, description, null, attacker, target, false, false, null);
    }

    /**
     * Factory for mate-delivering motif occurrences with attacker/target (e.g. checkmate, back-rank
     * mate, smothered mate).
     */
    public static MotifOccurrence withMate(
        PositionContext ctx,
        String description,
        @Nullable String attacker,
        @Nullable String target) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null;
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(
          ply, ctx.moveNumber(), side, description, null, attacker, target, false, true, null);
    }

    /**
     * Factory for PIN occurrences with attacker, target and pin type.
     *
     * @param pinType "ABSOLUTE" when pinned to king, "RELATIVE" when pinned to another valuable
     *     piece
     */
    public static MotifOccurrence pin(
        PositionContext ctx, String description, String attacker, String target, String pinType) {
      boolean movedWhite = !ctx.whiteToMove();
      int ply = movedWhite ? 2 * ctx.moveNumber() - 1 : 2 * (ctx.moveNumber() - 1);
      if (ply <= 0) return null;
      String side = movedWhite ? "white" : "black";
      return new MotifOccurrence(
          ply, ctx.moveNumber(), side, description, null, attacker, target, false, false, pinType);
    }
  }
}
