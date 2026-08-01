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

  private static final String STRANDED_MESSAGE =
      "Abandoned: no worker took ownership before the staleness cutoff (orphaned by restart or a"
          + " failed inline dispatch). Re-submit to try again.";

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
              rs.getBoolean("exclude_bullet"));

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
      Duration staleAfter,
      Instant now) {
    String key = dedupeKey(player, platform, startMonth, endMonth, excludeBullet);

    // Retire an abandoned holder first, so a dead request cannot keep its replacement out. Without
    // this the unique constraint would turn #1250's stranded row from "dedupe keeps answering with
    // it" into "the database refuses the replacement" — the same lockout, enforced harder.
    reclaimStaleForKey(key, staleAfter, now);

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
        UUID id = insert(key, player, platform, startMonth, endMonth, excludeBullet);
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
      boolean excludeBullet) {
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
                       created_at, updated_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
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

  private int reclaimStaleForKey(String dedupeKey, Duration staleAfter, Instant now) {
    return retire(dedupeKey, staleAfter, now);
  }

  @Override
  public int reclaimStale(Duration staleAfter, Instant now) {
    int reclaimed = retire(null, staleAfter, now);
    if (reclaimed > 0) {
      LOG.warn(
          "Retired {} stranded indexing request(s) not updated since {}",
          reclaimed,
          now.minus(staleAfter));
    }
    return reclaimed;
  }

  /**
   * Marks abandoned live requests FAILED and releases the dedupe slot each one holds. Named
   * parameters rather than positional ones, and two call sites rather than one with a conditional
   * predicate: the sweep and the single-key reclaim differ only by one clause, and building that
   * clause by concatenation meant the caller had to keep a {@code bind(3, ...)} in step with it.
   */
  private static final String RETIRE_SQL =
      """
      UPDATE indexing_requests
      SET status = 'FAILED', error_message = :message, dedupe_key = NULL, updated_at = :now
      WHERE status IN ('PENDING', 'PROCESSING')
        AND updated_at < :cutoff
      """;

  private int retire(String keyOrNull, Duration staleAfter, Instant now) {
    String sql = keyOrNull == null ? RETIRE_SQL : RETIRE_SQL + "  AND dedupe_key = :key\n";
    return jdbi.withHandle(
        h -> {
          var update =
              h.createUpdate(sql)
                  .bind("message", STRANDED_MESSAGE)
                  .bindByType("now", toUtcWallClock(now), LocalDateTime.class)
                  .bindByType("cutoff", toUtcWallClock(now.minus(staleAfter)), LocalDateTime.class);
          if (keyOrNull != null) {
            update.bind("key", keyOrNull);
          }
          return update.execute();
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
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      Duration staleAfter,
      Instant now) {
    return jdbi.withHandle(
        h ->
            h.createQuery(
                    """
                    SELECT * FROM indexing_requests
                    WHERE player = ? AND platform = ? AND start_month = ? AND end_month = ?
                      AND exclude_bullet = ?
                      AND status IN ('PENDING', 'PROCESSING')
                      AND updated_at >= ?
                    ORDER BY created_at ASC
                    LIMIT 1
                    """)
                .bind(0, player)
                .bind(1, platform)
                .bind(2, startMonth)
                .bind(3, endMonth)
                .bind(4, excludeBullet)
                .bindByType(5, toUtcWallClock(now.minus(staleAfter)), LocalDateTime.class)
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
    boolean terminal = !"PENDING".equals(status) && !"PROCESSING".equals(status);
    String sql =
        terminal
            ? """
            UPDATE indexing_requests
            SET status = ?, error_message = ?, games_indexed = ?, updated_at = ?,
                dedupe_key = NULL
            WHERE id = ?
            """
            // A non-terminal write may only move a row that is still live. Without the status
            // guard a retired request can be resurrected: reclaimStale marks a stalled request
            // FAILED and NULLs its key on the assumption its owner is dead, but nothing fences
            // that owner, and IndexWorker writes PROCESSING once per month rather than once per
            // run. The next such write would flip the row back to PROCESSING while leaving
            // dedupe_key NULL — a live request holding no slot. Its replacement already holds the
            // key, so the constraint cannot see the violation, and a skipCache submit (which
            // bypasses the dedupe read) would then start exactly the rival run this change exists
            // to prevent.
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
