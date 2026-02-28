package com.muchq.games.one_d4.api.dto;

import java.time.Instant;
import java.util.UUID;

public record GameFeature(
    UUID id,
    UUID requestId,
    String gameUrl,
    String platform,
    String whiteUsername,
    String blackUsername,
    Integer whiteElo,
    Integer blackElo,
    String timeClass,
    String eco,
    String result,
    Instant playedAt,
    Integer numMoves,
    Instant indexedAt,
    String pgn) {}
