package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.io.Closeable;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.time.Duration;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The concurrency guarantees behind {@code motif_occurrences} writes, against the engine they are
 * guarantees about.
 *
 * <p>{@link ConcurrentFlushTest} covers what H2 can show: that the delete and insert are one
 * transaction, and that a reader never sees a half-applied flush. Two of the properties this change
 * relies on are not observable there, because they are statements about PostgreSQL's READ COMMITTED
 * behaviour and H2's MVStore does not reproduce them. Asserting them on H2 would produce tests that
 * pass whether or not the code is right — the same vacuity trap this suite exists to avoid.
 *
 * <ul>
 *   <li><b>The ownership probe must take a row lock.</b> Nothing sets an isolation level, so a
 *       plain {@code SELECT} inside the flush transaction is a snapshot, not a claim: a concurrent
 *       takeover commits in the gap and the flush writes rows for a request it no longer owns. The
 *       window is the whole flush, not an instant.
 *   <li><b>Being transactional is not enough to make two writers safe against each other.</b> A
 *       {@code DELETE} blocked on another transaction's uncommitted delete re-checks the rows it
 *       blocked on, but not rows that transaction <em>inserted</em> — those fall outside its
 *       statement snapshot. So two transactional writers over one game still interleave as delete,
 *       delete, insert, insert unless they first contend for something shared. Both take the game's
 *       {@code game_features} row.
 * </ul>
 *
 * <p>Runs against the real postgres CI provides via {@code PG_TEST_DB_URL}; skips when that is
 * unset. A skipped run proves nothing about either property — say so rather than reporting green.
 */
public class PostgresConcurrentWriteTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_concurrent_test";
  private static final String GAME_URL = "https://chess.com/game/pg-contended";
  private static final String OWNER_A = "host-a/1/aaaa";
  private static final String OWNER_B = "host-b/2/bbbb";
  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");

  /**
   * How long a writer holds its transaction open while the other one runs. Long enough that the
   * second writer is certainly past the point where it would read a stale snapshot.
   */
  private static final long HOLD_MILLIS = 750;

  private DataSource dataSource;
  private GameFeatureDao dao;
  private UUID requestId;
  private ExecutorService pool;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres concurrency suite");

    try (Connection conn = DriverManager.getConnection(jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }

    dataSource = DataSourceFactory.create(jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, false).run();
    dao = new GameFeatureDao(Jdbi.create(dataSource), false);
    requestId = insertClaimedRequest();
    pool = Executors.newFixedThreadPool(2);
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (pool != null) {
      pool.shutdownNow();
    }
    if (dataSource instanceof Closeable closeable) {
      closeable.close();
    }
    String rawUrl = System.getenv(DB_URL_ENV);
    if (rawUrl == null || rawUrl.isBlank()) {
      return;
    }
    try (Connection conn = DriverManager.getConnection(jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
    }
  }

  /**
   * A takeover that commits while a flush is in flight. Without {@code FOR UPDATE} on the probe the
   * flush reads its pre-takeover snapshot, believes it still owns the request, and commits rows
   * against a range someone else is indexing.
   */
  @Test
  @Timeout(60)
  public void aFlushCannotCommitAgainstARequestThatChangedHandsWhileItRan() throws Exception {
    CountDownLatch rivalHoldsRow = new CountDownLatch(1);
    CountDownLatch neverReleased = new CountDownLatch(1);

    Future<?> rival =
        pool.submit(
            () -> {
              try (Connection conn = dataSource.getConnection()) {
                conn.setAutoCommit(false);
                try (var ps =
                    conn.prepareStatement(
                        "UPDATE indexing_requests SET owner_id = ?, lease_expires_at = ?"
                            + " WHERE id = ?")) {
                  ps.setString(1, OWNER_B);
                  ps.setObject(2, utcWallClock(NOW.plus(Duration.ofMinutes(5))));
                  ps.setObject(3, requestId);
                  ps.executeUpdate();
                }
                rivalHoldsRow.countDown();
                neverReleased.await(HOLD_MILLIS, TimeUnit.MILLISECONDS);
                conn.commit();
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });

    assertThat(rivalHoldsRow.await(20, TimeUnit.SECONDS)).isTrue();
    Future<Boolean> flush =
        pool.submit(
            () ->
                dao.flushOwned(
                    requestId,
                    OWNER_A,
                    NOW,
                    List.of(gameFeature()),
                    Map.of(GAME_URL, occurrences())));

    assertThat(flush.get(30, TimeUnit.SECONDS))
        .as("the flush committed against a request that had already changed hands")
        .isFalse();
    rival.get(30, TimeUnit.SECONDS);
    assertThat(countOccurrences()).isZero();
  }

  /**
   * Reanalysis and an indexing flush over the same game. Each is a transaction; that is not what
   * makes them safe against each other, and this is the engine where the difference shows.
   */
  @Test
  @Timeout(60)
  public void reanalysisAndAFlushOverOneGameDoNotDuplicateOccurrences() throws Exception {
    assertThat(
            dao.flushOwned(
                requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences())))
        .isTrue();
    assertThat(countOccurrences()).isEqualTo(2);

    CountDownLatch bothStarted = new CountDownLatch(2);

    Future<?> reanalysis =
        pool.submit(
            () -> {
              await(bothStarted);
              dao.replaceOccurrences(List.of(GAME_URL), Map.of(GAME_URL, occurrences()));
            });
    Future<?> flush =
        pool.submit(
            () -> {
              await(bothStarted);
              dao.flushOwned(
                  requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));
            });

    reanalysis.get(30, TimeUnit.SECONDS);
    flush.get(30, TimeUnit.SECONDS);

    assertThat(countOccurrences())
        .as("reanalysis and an indexing flush left two copies of every motif for this game")
        .isEqualTo(2);
  }

  // --- helpers ---------------------------------------------------------------------------------

  private static void await(CountDownLatch latch) {
    latch.countDown();
    try {
      latch.await(20, TimeUnit.SECONDS);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  private static LocalDateTime utcWallClock(Instant instant) {
    return instant.atOffset(ZoneOffset.UTC).toLocalDateTime();
  }

  private Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences() {
    return Map.of(
        Motif.CHECK,
        List.of(
            new GameFeatures.MotifOccurrence(
                5, 3, "white", "check", null, null, null, false, false, null),
            new GameFeatures.MotifOccurrence(
                9, 5, "black", "check", null, null, null, false, false, null)));
  }

  private GameFeature gameFeature() {
    return new GameFeature(
        null,
        requestId,
        GAME_URL,
        "CHESS_COM",
        "w",
        "b",
        1500,
        1500,
        null,
        null,
        "blitz",
        "B00",
        null,
        null,
        "1-0",
        Instant.parse("2024-01-15T00:00:00Z"),
        20,
        Instant.parse("2024-01-16T00:00:00Z"),
        "pgn");
  }

  private UUID insertClaimedRequest() throws Exception {
    UUID id = UUID.randomUUID();
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status, owner_id, lease_expires_at) VALUES (?, 'p', 'CHESS_COM', '2024-01',"
                    + " '2024-01', 'PROCESSING', ?, ?)")) {
      ps.setObject(1, id);
      ps.setString(2, OWNER_A);
      ps.setObject(3, utcWallClock(NOW.plus(Duration.ofMinutes(5))));
      ps.executeUpdate();
    }
    return id;
  }

  private int countOccurrences() {
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("SELECT COUNT(*) FROM motif_occurrences WHERE game_url = ?")) {
      ps.setString(1, GAME_URL);
      try (var rs = ps.executeQuery()) {
        rs.next();
        return rs.getInt(1);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * Same shape as {@link PostgresAggregateCompatTest}: a scratch database, one schema per suite.
   */
  private static String jdbcUrl(String rawUrl, String schema) {
    URI uri = URI.create(rawUrl.startsWith("jdbc:") ? rawUrl.substring("jdbc:".length()) : rawUrl);
    String userInfo = uri.getUserInfo();
    StringBuilder sb = new StringBuilder("jdbc:postgresql://");
    sb.append(uri.getHost());
    if (uri.getPort() > 0) {
      sb.append(':').append(uri.getPort());
    }
    sb.append(uri.getPath() == null || uri.getPath().isEmpty() ? "/postgres" : uri.getPath());
    StringBuilder params = new StringBuilder();
    if (userInfo != null && !userInfo.isEmpty()) {
      String[] parts = userInfo.split(":", 2);
      params.append("user=").append(URLEncoder.encode(parts[0], StandardCharsets.UTF_8));
      if (parts.length > 1) {
        params.append("&password=").append(URLEncoder.encode(parts[1], StandardCharsets.UTF_8));
      }
    }
    if (schema != null) {
      if (params.length() > 0) {
        params.append('&');
      }
      params.append("currentSchema=").append(schema);
    }
    if (params.length() > 0) {
      sb.append('?').append(params);
    }
    return sb.toString();
  }
}
