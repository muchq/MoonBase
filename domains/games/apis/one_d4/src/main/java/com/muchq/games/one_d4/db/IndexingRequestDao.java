package com.muchq.games.one_d4.db;

import java.sql.SQLException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.mapper.RowMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexingRequestDao implements IndexingRequestStore {
  private static final Logger LOG = LoggerFactory.getLogger(IndexingRequestDao.class);

  /**
   * How many times {@link #createOrAdopt} may lose the race before giving up. A loser normally
   * adopts on its next pass; it only goes round again if the winner reached a terminal status in
   * the gap, which frees the key and makes inserting correct again. Bounded because unbounded retry
   * on a permanently unsatisfiable condition is a hang, not a fix.
   */
  private static final int MAX_CLAIM_ATTEMPTS = 5;

  private static final RowMapper<IndexingRequest> ROW_MAPPER =
      (rs, ctx) ->
          new IndexingRequest(
              UUID.fromString(rs.getString("id")),
              rs.getString("player"),
              rs.getString("platform"),
              rs.getString("start_month"),
              rs.getString("end_month"),
              rs.getString("status"),
              rs.getTimestamp("created_at").toInstant(),
              rs.getTimestamp("updated_at").toInstant(),
              rs.getString("error_message"),
              rs.getInt("games_indexed"),
              rs.getBoolean("exclude_bullet"),
              rs.getBoolean("skip_cache"),
              rs.getInt("attempts"));

  private final Jdbi jdbi;
  private final Clock clock;

  public IndexingRequestDao(Jdbi jdbi) {
    this(jdbi, Clock.systemUTC());
  }

  /**
   * @param clock stamps {@code created_at} and {@code updated_at}. Every write goes through it and
   *     every staleness comparison is made against it, so the table sits on one clock — see {@link
   *     #toUtcWallClock}.
   */
  public IndexingRequestDao(Jdbi jdbi, Clock clock) {
    this.jdbi = jdbi;
    this.clock = clock;
  }

  /**
   * {@code created_at} and {@code updated_at} are TIMESTAMP WITHOUT TIME ZONE, so they hold a wall
   * clock rather than an instant, and the convention here — as in {@link GameFeatureDao} — is that
   * the stored wall clock is UTC. Both sides of every comparison are bound through this, so
   * staleness is measured in one frame of reference.
   *
   * <p>This matters more here than it did for {@code game_features}. That column had the same split
   * — written by the database's {@code now()}, compared against a JVM threshold — and it drifted
   * with host skew and the server's timezone (#1268). Retention's window there is 7 days, so drift
   * moved a boundary. {@link RetentionPolicy#STALE_REQUEST} is <em>one hour</em>, and three things
   * reach that far: clock skew between the app host and the database host, which are different
   * machines; the documented two-JVM deployment, where REST and MCP write and compare against one
   * Postgres and a zone difference between them is a straight offset; and DST, where a local wall
   * clock repeats or skips exactly one hour — exactly the window — so at a boundary the sweep would
   * either ignore every strand or retire every healthy request. Pinning UTC removes all three.
   */
  private static LocalDateTime toUtcWallClock(Instant instant) {
    return instant.atOffset(ZoneOffset.UTC).toLocalDateTime();
  }

  /**
   * The value stored in {@code dedupe_key} while a request is live. Must render identically to the
   * SQL backfill in {@link Migration}, or a row migrated from the pre-constraint schema would not
   * be found by a Java-side lookup.
   *
   * <p>Player goes last on purpose. It is the only free-form component — platform is validated
   * against a closed set, the months are {@code YearMonth}-parsed, and the flag is a boolean — so
   * putting it at the tail makes the encoding unambiguous without escaping the delimiter, whatever
   * a username happens to contain.
   */
  static String dedupeKey(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
    return platform + "|" + startMonth + "|" + endMonth + "|" + excludeBullet + "|" + player;
  }

  @Override
  public Claim createOrAdopt(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache,
      Duration staleAfter,
      Instant now) {
    String key = dedupeKey(player, platform, startMonth, endMonth, excludeBullet);

    // Settle any abandoned holder first. Usually that means releasing it — the work is still
    // queued and this caller should adopt it rather than start a rival — and retiring it only once
    // its attempts are spent, which frees the key so an insert can succeed.
    reclaim(key, staleAfter, now);

    RuntimeException lastConflict = null;
    for (int attempt = 0; attempt < MAX_CLAIM_ATTEMPTS; attempt++) {
      Optional<IndexingRequest> holder = findByDedupeKey(key);
      if (holder.isPresent()) {
        return new Claim(holder.get(), false);
      }
      // Each step is its own transaction rather than one enclosing it. On Postgres a constraint
      // violation aborts the whole transaction, so an insert-then-recover inside a single one
      // would need a savepoint; separate statements make the recovery path identical on both
      // engines. The constraint, not the transaction boundary, is what makes this safe: exactly
      // one racer's insert can succeed.
      try {
        UUID id = insert(key, player, platform, startMonth, endMonth, excludeBullet, skipCache);
        return new Claim(
            findById(id)
                .orElseThrow(
                    () ->
                        new IllegalStateException(
                            "inserted request " + id + " vanished before it could be read back")),
            true);
      } catch (RuntimeException e) {
        if (!isUniqueViolation(e)) {
          throw e;
        }
        // Lost the race. Go round: normally the winner is there to adopt, but if it finished in
        // the gap the key is free again and inserting is the right move.
        //
        // Keep the exception. isUniqueViolation matches SQLState class 23 broadly, so a NOT NULL
        // or check violation lands here too; without the cause, the throw below would blame
        // contention for what is really a bad column value.
        lastConflict = e;
      }
    }
    throw new IllegalStateException(
        "Could not claim or adopt an indexing request for "
            + key
            + " after "
            + MAX_CLAIM_ATTEMPTS
            + " attempts",
        lastConflict);
  }

  /** Throws on conflict; {@link #createOrAdopt} decides whether that is recoverable. */
  private UUID insert(
      String dedupeKey,
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache) {
    UUID id = UUID.randomUUID();
    // created_at/updated_at are stamped here rather than left to the column DEFAULT, so they come
    // from the same clock the staleness predicates compare against.
    LocalDateTime stamp = toUtcWallClock(clock.instant());
    jdbi.useHandle(
        h ->
            h.createUpdate(
                    """
                    INSERT INTO indexing_requests
                      (id, player, platform, start_month, end_month, exclude_bullet, dedupe_key,
                       created_at, updated_at, skip_cache, attempts)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)
                    """)
                .bind(0, id)
                .bind(1, player)
                .bind(2, platform)
                .bind(3, startMonth)
                .bind(4, endMonth)
                .bind(5, excludeBullet)
                .bind(6, dedupeKey)
                .bindByType(7, stamp, LocalDateTime.class)
                .bindByType(8, stamp, LocalDateTime.class)
                .bind(9, skipCache)
                .execute());
    return id;
  }

  /**
   * True when the cause chain carries SQLState class 23 (integrity constraint violation). Matching
   * on the class rather than the exact code keeps this working across both engines — Postgres and
   * H2 both raise 23505 for a duplicate key, but only the class is guaranteed by the standard.
   */
  private static boolean isUniqueViolation(Throwable e) {
    for (Throwable t = e; t != null; t = t.getCause()) {
      if (t instanceof SQLException sqlException) {
        String state = sqlException.getSQLState();
        if (state != null && state.startsWith("23")) {
          return true;
        }
      }
      if (t.getCause() == t) {
        break;
      }
    }
    return false;
  }

  private Optional<IndexingRequest> findByDedupeKey(String dedupeKey) {
    return jdbi.withHandle(
        h ->
            h.createQuery("SELECT * FROM indexing_requests WHERE dedupe_key = ?")
                .bind(0, dedupeKey)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public int reclaimStale(Duration staleAfter, Instant now) {
    return reclaim(null, staleAfter, now);
  }

  /**
   * Returns abandoned work to the queue. An expired lease means the owner is gone, not that the
   * work is unwanted, so the row is unclaimed and left live for the next worker to take.
   *
   * <p>This is the half of #1279 that is easiest to get backwards. Before dispatch read from this
   * table an expired lease had to be retired: nothing was ever going to run that request again, so
   * freeing the dedupe slot and telling the user to resubmit was the only way out. Now the row
   * <em>is</em> the queue, and retiring it throws away work any worker could pick up. Clearing the
   * owner is enough — {@code dedupe_key} stays, because the range is still spoken for.
   */
  private static final String RELEASE_SQL =
      """
      UPDATE indexing_requests
      SET owner_id = NULL, updated_at = :now
      WHERE status IN ('PENDING', 'PROCESSING')
        AND owner_id IS NOT NULL
        AND lease_expires_at IS NOT NULL
        AND lease_expires_at <= :now
        AND attempts < :maxAttempts
      """;

  /**
   * The one case where abandoned work is still retired: it has been claimed {@link #MAX_ATTEMPTS}
   * times and every worker stopped before finishing.
   *
   * <p>Releasing is unbounded by construction, and that is the new hazard. A request that kills the
   * process handling it used to take that process's queue with it and stop there; once any worker
   * can claim it, the same request tours the fleet indefinitely and each lap costs another worker.
   * The counter is the only thing that separates "the last worker was unlucky" from "this request
   * is the problem".
   */
  private static final String RETIRE_POISONED_SQL =
      """
      UPDATE indexing_requests
      SET status = 'FAILED', error_message = :poisoned, dedupe_key = NULL, updated_at = :now,
          owner_id = NULL, lease_expires_at = NULL
      WHERE status IN ('PENDING', 'PROCESSING')
        AND attempts >= :maxAttempts
        AND (owner_id IS NULL
             OR lease_expires_at IS NULL
             OR lease_expires_at <= :now)
      """;

  /**
   * The backstop: a request nobody is running, that nothing has touched in {@code staleAfter},
   * while no worker anywhere holds a live lease.
   *
   * <p>Releasing work back to the queue is the right answer to a dead owner, but it cannot be the
   * only answer. If every worker dies, or the fleet is partitioned from this database, released
   * rows sit PENDING forever and the user is told nothing at all — which is worse than being told
   * the request failed. Eventually the answer has to be an answer.
   *
   * <p>The {@code NOT EXISTS} is what stops this from re-becoming the bug two changes were spent
   * removing. Plain age cannot distinguish "nothing is serving this" from "my turn has not come
   * yet": a single worker draining a deep backlog leaves rows at the back untouched for as long as
   * the backlog takes, and retiring those tells a user to resubmit work that was about to run.
   *
   * <p>Two probes, and it takes both, because either alone gets it wrong in a way that matters.
   *
   * <p>A live lease <em>right now</em> is the obvious signal and it is not enough on its own:
   * leases are held in bursts, and between one job's terminal write and the next claim there is a
   * real moment with none held anywhere. Sampling there declares a healthy fleet dead — reproduced
   * on both engines, forty-nine queued rows FAILED in one statement by a millisecond-wide gap.
   * Worse than the bug the guard exists to prevent.
   *
   * <p>A recent {@code lease_expires_at} covers that gap, and the column choice is the point. A
   * plain "anything touched lately" would count submits, so a user submitting into a fleet of dead
   * workers would suppress their own answer indefinitely — only a worker sets a lease. And it has
   * to be the lease rather than {@code owner_id}, because a successful run clears the owner on its
   * way out: the evidence that a worker was here would be erased by that worker finishing. So
   * terminal writes and releases now leave {@code lease_expires_at} where it is. Nothing reads it
   * as ownership — every such predicate also requires {@code owner_id} and a live status — and it
   * becomes an honest record of when a worker last held this row.
   *
   * <p>The probe excludes the row being judged. Its own lease mark is not evidence that anyone is
   * working <em>now</em> — it is the record of the worker that abandoned it, which is the whole
   * reason it is a candidate.
   *
   * <p>Requiring both means this errs toward silence rather than toward false failure, which is the
   * right way round — a wrong FAILED destroys queued work, a late FAILED only delays an answer. The
   * residual case is a worker wedged mid-run: its heartbeat renews from a separate thread, so both
   * probes stay positive, and that one live lease suppresses this arm for every queued row in the
   * table rather than only its own. No liveness probe can close it — the worker is alive, and what
   * is unknown is whether the run is progressing — so bounding it needs a ceiling on run duration.
   * Tracked in #1282.
   */
  private static final String RETIRE_ABANDONED_SQL =
      """
      UPDATE indexing_requests
      SET status = 'FAILED', error_message = :stalled, dedupe_key = NULL, updated_at = :now,
          owner_id = NULL, lease_expires_at = NULL
      WHERE status IN ('PENDING', 'PROCESSING')
        AND (owner_id IS NULL
             OR lease_expires_at IS NULL
             OR lease_expires_at <= :now)
        AND updated_at < :cutoff
        AND NOT EXISTS (
          SELECT 1 FROM indexing_requests live
          WHERE live.owner_id IS NOT NULL AND live.lease_expires_at > :now)
        AND NOT EXISTS (
          SELECT 1 FROM indexing_requests recent
          WHERE recent.id <> indexing_requests.id AND recent.lease_expires_at >= :cutoff)
      """;

  static final String STALLED_MESSAGE =
      "Abandoned: no indexing worker has picked this up, and none is running anywhere. Re-submit"
          + " once indexing is available again.";

  private int reclaim(String keyOrNull, Duration staleAfter, Instant now) {
    String keyClause = keyOrNull == null ? "" : "  AND dedupe_key = :key\n";
    return jdbi.inTransaction(
        h -> {
          // Order matters twice, for two different reasons, and neither is the one that looks
          // obvious. A row at the attempt limit cannot match RELEASE_SQL at all — that statement
          // carries its own attempts guard — so this is not about stopping an extra lap.
          //
          // Poisoned before stalled: a row whose attempts are spent is also unheld and may well be
          // old, so it matches the stalled arm too. Both retire it; only one of them tells the
          // user the truth about why.
          //
          // Stalled before released: releasing stamps updated_at, which would make the row look
          // freshly touched and hide it from the staleness the stalled arm looks for — costing the
          // user another full window of silence.
          var retire =
              h.createUpdate(RETIRE_POISONED_SQL + keyClause)
                  .bind("poisoned", POISONED_MESSAGE)
                  .bind("maxAttempts", MAX_ATTEMPTS)
                  .bindByType("now", toUtcWallClock(now), LocalDateTime.class);
          var release =
              h.createUpdate(RELEASE_SQL + keyClause)
                  .bind("maxAttempts", MAX_ATTEMPTS)
                  .bindByType("now", toUtcWallClock(now), LocalDateTime.class);
          if (keyOrNull != null) {
            retire.bind("key", keyOrNull);
            release.bind("key", keyOrNull);
          }
          var abandoned =
              h.createUpdate(RETIRE_ABANDONED_SQL + keyClause)
                  .bind("stalled", STALLED_MESSAGE)
                  .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                  .bindByType("cutoff", toUtcWallClock(now.minus(staleAfter)), LocalDateTime.class);
          if (keyOrNull != null) {
            abandoned.bind("key", keyOrNull);
          }
          int retired = retire.execute();
          // Before the release, deliberately: releasing sets updated_at = now, which would make
          // every row look freshly touched and hide the very staleness this arm looks for.
          int stalled = abandoned.execute();
          int released = release.execute();
          if (stalled > 0) {
            LOG.warn(
                "Retired {} indexing request(s) that no worker has taken, with no live worker"
                    + " anywhere",
                stalled);
          }
          if (released > 0) {
            LOG.info("Returned {} abandoned indexing request(s) to the queue", released);
          }
          if (retired > 0) {
            LOG.warn(
                "Retired {} indexing request(s) after {} failed attempts", retired, MAX_ATTEMPTS);
          }
          return retired + stalled + released;
        });
  }

  @Override
  public Optional<IndexingRequest> findById(UUID id) {
    return jdbi.withHandle(
        h ->
            h.createQuery("SELECT * FROM indexing_requests WHERE id = ?")
                .bind(0, id)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public Optional<IndexingRequest> findExistingRequest(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
    return jdbi.withHandle(
        h ->
            h.createQuery(
                    """
                    SELECT * FROM indexing_requests
                    WHERE player = :player AND platform = :platform
                      AND start_month = :startMonth AND end_month = :endMonth
                      AND exclude_bullet = :excludeBullet
                      AND status IN ('PENDING', 'PROCESSING')
                      AND attempts < :maxAttempts
                    ORDER BY created_at ASC, id ASC
                    LIMIT 1
                    """)
                .bind("player", player)
                .bind("platform", platform)
                .bind("startMonth", startMonth)
                .bind("endMonth", endMonth)
                .bind("excludeBullet", excludeBullet)
                .bind("maxAttempts", MAX_ATTEMPTS)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public List<IndexingRequest> listRecent(int limit) {
    return jdbi.withHandle(
        h ->
            h.createQuery("SELECT * FROM indexing_requests ORDER BY created_at DESC LIMIT ?")
                .bind(0, limit)
                .map(ROW_MAPPER)
                .list());
  }

  @Override
  public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
    // A terminal status releases the dedupe slot: the same range becomes requestable again the
    // moment the work stops being in flight. Leaving the key behind would make one COMPLETED
    // request block that range permanently.
    //
    // Nothing here is fenced on ownership, so this is the unowned path: it is for callers writing
    // about a request no worker ever claimed — the inline-dispatch failure in IndexRequestService,
    // and tests. A worker presents its token and goes through updateStatusOwned instead, which is
    // what stops a lapsed owner from stamping COMPLETED over a range someone else now holds.
    boolean terminal = isTerminal(status);
    String sql =
        terminal
            ? """
            UPDATE indexing_requests
            SET status = ?, error_message = ?, games_indexed = ?, updated_at = ?,
                dedupe_key = NULL, owner_id = NULL, lease_expires_at = NULL
            WHERE id = ?
            """
            // A non-terminal write may only move a row that is still live. Without the status
            // guard a retired request can be resurrected: reclaimStale marks a stalled request
            // FAILED and NULLs its key on the assumption its owner is dead, but nothing fences
            // that owner, and IndexWorker writes PROCESSING once per month rather than once per
            // run. The next such write would flip the row back to PROCESSING while leaving
            // dedupe_key NULL — a live request holding no slot. Its replacement already holds the
            // key, so the constraint cannot see the violation, and the next submit for that
            // range would then start exactly the rival run this change exists to prevent.
            //
            // Restoring the key here instead would be wrong: the replacement holds it, so the
            // UPDATE would fail on the constraint. Refusing the resurrection is the only shape
            // that keeps one live row per tuple.
            : """
            UPDATE indexing_requests
            SET status = ?, error_message = ?, games_indexed = ?, updated_at = ?
            WHERE id = ? AND status IN ('PENDING', 'PROCESSING')
            """;
    LocalDateTime stamp = toUtcWallClock(clock.instant());
    int updated =
        jdbi.withHandle(
            h ->
                h.createUpdate(sql)
                    .bind(0, status)
                    .bind(1, errorMessage)
                    .bind(2, gamesIndexed)
                    .bindByType(3, stamp, LocalDateTime.class)
                    .bind(4, id)
                    .execute());
    if (updated == 0 && !terminal) {
      LOG.warn(
          "Refused to move request {} to {}: it is no longer live, most likely retired as stale."
              + " A replacement request owns this range now.",
          id,
          status);
    }
  }

  private static boolean isTerminal(String status) {
    return !"PENDING".equals(status) && !"PROCESSING".equals(status);
  }

  @Override
  public boolean updateStatusOwned(
      UUID id, String ownerId, String status, String errorMessage, int gamesIndexed, Instant now) {
    // The lease is checked in the same statement that writes, so there is no window between
    // establishing ownership and using it. Requiring an unexpired lease and not merely a matching
    // owner_id is the stricter of the two available readings, and the right one: past expiry the
    // system has already licensed a takeover, so a write from the old owner is racing a claim it
    // cannot see. Recovering from a hiccup is renewLease's job, not this one's.
    String sql =
        isTerminal(status)
            ? """
            UPDATE indexing_requests
            SET status = :status, error_message = :message, games_indexed = :games,
                updated_at = :now, dedupe_key = NULL, owner_id = NULL
            WHERE id = :id AND owner_id = :owner
              AND status IN ('PENDING', 'PROCESSING')
              AND lease_expires_at > :now
            """
            : """
            UPDATE indexing_requests
            SET status = :status, error_message = :message, games_indexed = :games,
                updated_at = :now
            WHERE id = :id AND owner_id = :owner
              AND status IN ('PENDING', 'PROCESSING')
              AND lease_expires_at > :now
            """;
    int updated =
        jdbi.withHandle(
            h ->
                h.createUpdate(sql)
                    .bind("status", status)
                    .bind("message", errorMessage)
                    .bind("games", gamesIndexed)
                    .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                    .bind("id", id)
                    .bind("owner", ownerId)
                    .execute());
    if (updated == 0) {
      LOG.warn(
          "Refused to move request {} to {}: {} no longer holds the lease.", id, status, ownerId);
    }
    return updated > 0;
  }

  static final int MAX_ATTEMPTS = IndexingRequestStore.MAX_ATTEMPTS;

  static final String POISONED_MESSAGE =
      "Abandoned: this request was attempted "
          + MAX_ATTEMPTS
          + " times and each worker stopped before finishing it. Something about this range fails"
          + " repeatedly rather than transiently.";

  /**
   * How deep {@link #claimNext} looks. One candidate would make every instance race for the same
   * row and all but one fail each pass; a handful lets them spread out without reordering the queue
   * in any way a user could notice.
   */
  private static final int CLAIM_CANDIDATES = 8;

  @Override
  public Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now) {
    // Read candidates, then try to claim each. One statement would want FOR UPDATE SKIP LOCKED and
    // H2 has none; the conditional UPDATE claim already performs is what makes this safe without
    // it, since at most one racer's WHERE can match a given row. Losing costs a retry against the
    // next candidate, not correctness.
    List<UUID> candidates =
        jdbi.withHandle(
            h ->
                h.createQuery(
                        """
                        SELECT id FROM indexing_requests
                        WHERE status IN ('PENDING', 'PROCESSING')
                          AND attempts < :maxAttempts
                          AND (owner_id IS NULL
                               OR lease_expires_at IS NULL
                               OR lease_expires_at <= :now)
                        ORDER BY created_at ASC, id ASC
                        LIMIT :limit
                        """)
                    .bind("maxAttempts", MAX_ATTEMPTS)
                    .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                    .bind("limit", CLAIM_CANDIDATES)
                    .mapTo(UUID.class)
                    .list());

    for (UUID id : candidates) {
      if (claim(id, ownerId, lease, now)) {
        return findById(id);
      }
    }
    return Optional.empty();
  }

  @Override
  public boolean claim(UUID id, String ownerId, Duration lease, Instant now) {
    // Unclaimed, or claimed by a lease that has run out. Expressed as one conditional UPDATE so
    // two instances racing for the same request cannot both win: the row lock decides it, and the
    // loser's WHERE no longer matches.
    int taken =
        jdbi.withHandle(
            h ->
                h.createUpdate(
                        """
                        UPDATE indexing_requests
                        SET owner_id = :owner, lease_expires_at = :expires, updated_at = :now,
                            attempts = CASE WHEN owner_id = :owner THEN attempts
                                            ELSE attempts + 1 END
                        WHERE id = :id
                          AND status IN ('PENDING', 'PROCESSING')
                          AND (owner_id IS NULL
                               OR owner_id = :owner
                               OR lease_expires_at IS NULL
                               OR lease_expires_at <= :now)
                        """)
                    .bind("owner", ownerId)
                    .bindByType("expires", toUtcWallClock(now.plus(lease)), LocalDateTime.class)
                    .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                    .bind("id", id)
                    .execute());
    return taken > 0;
  }

  @Override
  public boolean renewLease(UUID id, String ownerId, Duration lease, Instant now) {
    int renewed =
        jdbi.withHandle(
            h ->
                h.createUpdate(
                        """
                        UPDATE indexing_requests
                        SET lease_expires_at = :expires, updated_at = :now
                        WHERE id = :id
                          AND owner_id = :owner
                          AND status IN ('PENDING', 'PROCESSING')
                        """)
                    .bindByType("expires", toUtcWallClock(now.plus(lease)), LocalDateTime.class)
                    .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                    .bind("id", id)
                    .bind("owner", ownerId)
                    .execute());
    return renewed > 0;
  }

  @Override
  public boolean holdsLease(UUID id, String ownerId, Instant now) {
    return jdbi.withHandle(
        h ->
            h.createQuery(
                        """
                        SELECT COUNT(*) FROM indexing_requests
                        WHERE id = ? AND owner_id = ?
                          AND status IN ('PENDING', 'PROCESSING')
                          AND lease_expires_at > ?
                        """)
                    .bind(0, id)
                    .bind(1, ownerId)
                    .bindByType(2, toUtcWallClock(now), LocalDateTime.class)
                    .mapTo(Integer.class)
                    .one()
                > 0);
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    return jdbi.withHandle(
        h -> {
          int deleted =
              h.createUpdate(
                      """
                      DELETE FROM indexing_requests
                      WHERE created_at < ?
                        AND status NOT IN ('PENDING', 'PROCESSING')
                        AND NOT EXISTS (
                          SELECT 1 FROM game_features g
                          WHERE g.request_id = indexing_requests.id)
                      """)
                  .bindByType(0, toUtcWallClock(threshold), LocalDateTime.class)
                  .execute();
          if (deleted > 0) {
            LOG.debug("Deleted {} indexing requests older than {}", deleted, threshold);
          }
          return deleted;
        });
  }
}
