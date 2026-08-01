package com.muchq.games.one_d4.db;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

public interface IndexingRequestStore {

  /**
   * Atomically claims the live slot for this (player, platform, startMonth, endMonth,
   * excludeBullet), returning either a freshly created request or the one that already holds it.
   *
   * <p>This exists instead of a bare {@code create} because dedupe cannot be done as a read
   * followed by a write. Two identical submits — two REST threads, or REST and MCP, which are two
   * JVMs against one Postgres whenever {@code INDEXER_DB_URL} is set — can both find nothing and
   * both insert. {@code game_features} survives that on its {@code game_url} upsert, but {@code
   * motif_occurrences} does not: its flush deletes and re-inserts under fresh UUIDs in separate
   * transactions, so interleaved runs leave duplicated occurrence rows and every motif count for
   * those games reads double.
   *
   * <p>The caller must dispatch work only when {@link Claim#created()} is true. Adopting means
   * another caller already owns the work.
   *
   * <p>Implementations also retire rows stranded past {@code staleAfter} as part of the same atomic
   * step, so an abandoned request cannot hold the slot against its replacement.
   */
  Claim createOrAdopt(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      Duration staleAfter,
      Instant now);

  Optional<IndexingRequest> findById(UUID id);

  /**
   * Returns an existing request with the same (player, platform, startMonth, endMonth,
   * excludeBullet) that is PENDING or PROCESSING and was updated within {@code staleAfter}, if any.
   * Used to avoid creating duplicate indexing work.
   *
   * <p>The age bound is what keeps a request whose owner died from answering for it forever. See
   * {@link RetentionPolicy#STALE_REQUEST}.
   */
  Optional<IndexingRequest> findExistingRequest(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      Duration staleAfter,
      Instant now);

  List<IndexingRequest> listRecent(int limit);

  void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed);

  /**
   * Retires every PENDING/PROCESSING request last updated before {@code now - staleAfter} to
   * FAILED, freeing the dedupe slot each one holds. Returns the number retired.
   */
  int reclaimStale(Duration staleAfter, Instant now);

  /**
   * Deletes request rows created before {@code threshold} that no {@code game_features} row still
   * references. Returns the number deleted.
   *
   * <p>The reference check is belt and braces on top of sweep ordering: games are deleted first and
   * on a shorter clock, so by the time a request is old enough to sweep its games are already gone.
   * Checking anyway means the delete cannot raise a foreign key violation and abort the whole
   * retention pass even if the two clocks are ever reconfigured into overlap.
   */
  int deleteOlderThan(Instant threshold);

  /** The outcome of {@link #createOrAdopt}: the winning row, and whether this caller created it. */
  record Claim(IndexingRequest request, boolean created) {}

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
      int gamesIndexed,
      boolean excludeBullet) {}
}
