package com.muchq.games.one_d4.db;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

public interface IndexingRequestStore {
  UUID create(String player, String platform, String startMonth, String endMonth);

  Optional<IndexingRequest> findById(UUID id);

  /**
   * Returns an existing request with the same (player, platform, startMonth, endMonth) that is
   * PENDING or PROCESSING, if any. Used to avoid creating duplicate indexing work.
   */
  Optional<IndexingRequest> findExistingRequest(
      String player, String platform, String startMonth, String endMonth);

  List<IndexingRequest> listRecent(int limit);

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
      int gamesIndexed) {}
}
