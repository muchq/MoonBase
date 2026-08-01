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
   * Returns the live request with the same (player, platform, startMonth, endMonth, excludeBullet),
   * if there is one. Used to avoid creating duplicate indexing work.
   *
   * <p>"Live" here must mean exactly what it means to {@link #reclaimStale} — the same two arms,
   * negated. A claimed request is live while its lease holds; an unclaimed one is live until {@code
   * staleAfter}. Answering that question two different ways on one submit is a bug with a long
   * tail: this read runs first and short-circuits, so a laxer predicate here silently overrides the
   * stricter reclamation behind it, and a request whose worker was killed keeps being handed back
   * to callers for the full hour instead of the lease's few minutes.
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
   * Retires live requests nobody is working on, freeing the dedupe slot and the lease each one
   * holds. Returns the number retired.
   *
   * <p>Two different questions, deliberately kept apart:
   *
   * <ul>
   *   <li><b>Claimed, lease expired.</b> Someone took this and stopped renewing, so the owner is
   *       gone. Decided by {@link RetentionPolicy#LEASE}, in minutes.
   *   <li><b>Never claimed, and old.</b> Nobody ever picked it up. Decided by {@code staleAfter},
   *       in hours, because there is no owner whose silence could be measured — only the age of the
   *       row itself.
   * </ul>
   *
   * <p>Conflating those two is what made a slow request indistinguishable from a dead one: a
   * running worker that simply had nothing to report for an hour was retired underneath itself. A
   * request being worked on now renews, and renewal is a claim about the owner rather than about
   * progress, so it stays alive however long a month takes.
   *
   * <p>Retiring makes a request reclaimable; it does not hand it to anyone. There is no scanner for
   * expired leases and no re-dispatch — the row goes FAILED, the range becomes requestable, and the
   * user resubmits. Actual takeover is #1279.
   *
   * <p>The unclaimed arm still has the gap it always had, and ownership made its consequence worse:
   * a message waiting in a process-local queue has no owner to renew for it, so a backlog deeper
   * than {@code staleAfter} retires work that is owned and about to run — and the worker that
   * eventually reaches it now declines the retired row instead of indexing it anyway. Closing that
   * means dispatch pulling from this table instead of an in-memory queue (#1279).
   */
  int reclaimStale(Duration staleAfter, Instant now);

  /**
   * Takes ownership of a request for {@code lease}, if it is live and either unclaimed or held by a
   * lease that has already expired. Returns true if this caller now owns it.
   *
   * <p>Claiming is how a worker earns the right to write. Before this, liveness was inferred from
   * whether {@code updated_at} had moved recently, which cannot distinguish a worker that is slow
   * from one that is dead, and gives nothing to check a later write against.
   *
   * <p>{@code ownerId} identifies the <em>process</em>, not the thread or the run. It is the
   * fencing token every subsequent write is conditioned on, so it has to stay stable while a worker
   * holds the request and be unique across the instances competing for it.
   */
  boolean claim(UUID id, String ownerId, Duration lease, Instant now);

  /**
   * Extends this owner's lease. Returns false if the request is no longer live, or if {@code
   * owner_id} no longer names this caller — someone else has taken it, and the caller should stop.
   *
   * <p>Deliberately lenient about expiry, and that is not an oversight: a lease that has lapsed but
   * still names this owner has not been taken by anyone, so renewing it is recovery rather than a
   * second claim. A paused GC or a stalled connection pool can outlast a lease without anything
   * being wrong, and the alternative — abandoning a run nobody else wants — throws away work and
   * leaves the row stranded until the sweep.
   *
   * <p>What it will not do is re-take. Once {@code owner_id} has changed this returns false, and it
   * cannot race a takeover: both are single conditional UPDATEs on one row, so the row lock orders
   * them and the rival always wins. Refusing there is the point — the replacement is already
   * indexing that range, and reasserting would put two writers on the same games.
   */
  boolean renewLease(UUID id, String ownerId, Duration lease, Instant now);

  /**
   * True if {@code ownerId} still holds a live, unexpired lease on this request.
   *
   * <p>Has no production caller, and should not acquire one: asking this before a write would be a
   * check-then-act with a window in it, which is exactly what {@link #updateStatusOwned} and {@link
   * GameFeatureStore#flushOwned} exist to avoid by folding the question into the write. It is here
   * for tests and for diagnostics.
   */
  boolean holdsLease(UUID id, String ownerId, Instant now);

  /**
   * Writes a status, but only if {@code ownerId} still holds the lease. Returns false without
   * writing if it does not — the caller has lost the request and should stop.
   *
   * <p>The fenced counterpart to {@link #updateStatus}. A worker holds a token, so every write it
   * makes is conditioned on that token, and this is the request row's half of the same rule {@link
   * GameFeatureStore#flushOwned} applies to the data plane. Without it a worker whose lease lapsed
   * could still stamp COMPLETED — and a terminal write clears {@code dedupe_key} and the lease, so
   * it would free the slot out from under whoever legitimately owns the range now, and report the
   * dead run's game count as the answer.
   *
   * <p>Non-owner callers keep using {@link #updateStatus}: the inline-dispatch failure path in
   * {@code IndexRequestService} writes on behalf of a request no worker ever claimed, and has no
   * token to present.
   */
  boolean updateStatusOwned(
      UUID id, String ownerId, String status, String errorMessage, int gamesIndexed, Instant now);

  /**
   * Deletes terminal request rows created before {@code threshold} that no {@code game_features}
   * row still references. Returns the number deleted.
   *
   * <p>Two guards, both narrowing what this can destroy rather than relying on the caller.
   *
   * <p>The reference check is belt and braces on top of sweep ordering: games are deleted first and
   * on a shorter clock, so by the time a request is old enough to sweep its games are already gone.
   * Checking anyway means the delete cannot raise a foreign key violation and abort the whole
   * retention pass even if the two clocks are ever reconfigured into overlap.
   *
   * <p>The status check stops a PENDING or PROCESSING request from being deleted out from under a
   * running worker. Today that is unreachable — {@link RetentionPolicy#REQUEST} is thirty days
   * against a one-hour staleness cutoff, and the worker reclaims stale rows earlier in the same
   * pass — but it is unreachable because of how the caller is sequenced, which is exactly the kind
   * of guarantee that evaporates when someone reorders the caller.
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
