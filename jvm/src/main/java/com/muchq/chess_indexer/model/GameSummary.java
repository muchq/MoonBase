package com.muchq.chess_indexer.model;

import java.time.Instant;
import java.util.UUID;

public record GameSummary(
    UUID id,
    String platform,
    String gameUuid,
    Instant endTime,
    String whiteUsername,
    int whiteElo,
    String blackUsername,
    int blackElo,
    String result,
    String timeClass,
    String eco,
    boolean rated
) {}
