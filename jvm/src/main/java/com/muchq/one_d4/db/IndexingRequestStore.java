package com.muchq.one_d4.db;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;

public interface IndexingRequestStore {
    UUID create(String player, String platform, String startMonth, String endMonth);
    Optional<IndexingRequest> findById(UUID id);
    void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed);

    record IndexingRequest(
            UUID id,
            String player,
            String platform,
            String startMonth,
            String endMonth,
            String status,
            Instant createdAt,
            Instant updatedAt,
            String errorMessage,
            int gamesIndexed
    ) {}
}
