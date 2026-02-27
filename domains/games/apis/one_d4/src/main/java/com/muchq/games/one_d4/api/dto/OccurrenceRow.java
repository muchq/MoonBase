package com.muchq.games.one_d4.api.dto;

import org.jspecify.annotations.Nullable;

public record OccurrenceRow(
    int moveNumber,
    String side,
    String description,
    @Nullable String movedPiece,
    @Nullable String attacker,
    @Nullable String target,
    boolean isDiscovered,
    boolean isMate) {}
