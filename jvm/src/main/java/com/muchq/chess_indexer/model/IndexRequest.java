package com.muchq.chess_indexer.model;

import java.time.Instant;
import java.time.LocalDate;
import java.util.UUID;

public record IndexRequest(
    UUID id,
    String platform,
    String username,
    LocalDate startDate,
    LocalDate endDate,
    IndexRequestStatus status,
    int gamesIndexed,
    String errorMessage,
    Instant createdAt,
    Instant updatedAt
) {}
