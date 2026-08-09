package com.muchq.games.one_d4.api.dto;

import org.jspecify.annotations.Nullable;

/**
 * One motif occurrence in an ad-hoc analysis.
 *
 * <p>Close to {@link OccurrenceRow} but not the same type, and deliberately so: that one describes
 * a row read back out of storage, so it carries {@code gameUrl} and {@code motif} and has no {@code
 * ply}. This describes an occurrence in a PGN nobody indexed — there is no game URL to name, the
 * motif is the map key it arrives under, and {@code ply} is what distinguishes two occurrences
 * inside the same move number.
 *
 * <p>Field-for-field the wire shape {@code analyze_position} has always returned, so moving that
 * tool from in-process analysis to this endpoint changes no MCP client's parsing.
 */
public record AnalyzedOccurrence(
    int ply,
    int moveNumber,
    String side,
    String description,
    @Nullable String movedPiece,
    @Nullable String attacker,
    @Nullable String target,
    boolean isDiscovered,
    boolean isMate,
    @Nullable String pinType) {}
