package com.muchq.chess_indexer.model;

import java.time.Instant;
import java.util.UUID;

public record GameRecord(
    UUID id,
    String platform,
    String gameUuid,
    Instant endTime,
    boolean rated,
    String timeClass,
    String rules,
    String eco,
    String whiteUsername,
    int whiteElo,
    String blackUsername,
    int blackElo,
    String result,
    String pgn
) {}
