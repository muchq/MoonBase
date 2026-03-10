package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpRequest.BodyPublishers;
import java.net.http.HttpResponse;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.Timestamp;
import java.time.Instant;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import javax.sql.DataSource;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

/**
 * Full-stack regression test for H2 "Concurrent update in table INDEXED_PERIODS" ([90131]).
 *
 * <p>Starts a real Micronaut embedded server with all production wiring — IndexWorkerLifecycle poll
 * thread, RetentionWorker, HikariCP connection pool, Netty event loop — but replaces the chess.com
 * client with a fake and the IndexedPeriodStore with a latch-controlled wrapper.
 *
 * <p>Reproduces the exact production MVCC conflict deterministically:
 *
 * <ol>
 *   <li>Pause the worker just before upsertPeriod (via {@link LatchIndexedPeriodDao}).
 *   <li>Hold a JDBC write lock on the same row (uncommitted MERGE with autoCommit=false).
 *   <li>Release the worker — its MERGE blocks on our lock, then times out with [90131].
 * </ol>
 *
 * <p>Uses {@code LOCK_TIMEOUT=100} in the H2 URL to keep the test fast.
 */
public class ConcurrentIndexE2ETest {

  private static final String PLAYER = "concurrent_e2e_player";
  private static final String PLATFORM = "CHESS_COM";

  private EmbeddedServer server;
  private ApplicationContext ctx;
  private HttpClient httpClient;
  private String baseUrl;
  private FakeChessClient fakeClient;
  private LatchIndexedPeriodDao latchPeriodStore;

  @Before
  public void setUp() {
    ctx =
        ApplicationContext.builder()
            .properties(
                Map.of(
                    "indexer.db.url",
                    "jdbc:h2:mem:concurrent_e2e_"
                        + System.nanoTime()
                        + ";DB_CLOSE_DELAY=-1;LOCK_TIMEOUT=100",
                    "micronaut.server.port",
                    "-1"))
            .build()
            .start();

    server = ctx.getBean(EmbeddedServer.class);
    server.start();

    fakeClient = ctx.getBean(FakeChessClient.class);
    latchPeriodStore = ctx.getBean(LatchIndexedPeriodDao.class);
    httpClient = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @After
  public void tearDown() {
    if (server != null) server.stop();
    if (ctx != null) ctx.stop();
  }

  /**
   * Reproduces H2 MVCC error [90131] deterministically:
   *
   * <ol>
   *   <li>Seed a row in indexed_periods so the worker's MERGE does an UPDATE (not INSERT).
   *   <li>Submit an index request. Worker processes games, reaches upsertPeriod, blocks on latch.
   *   <li>Test thread: open connection with autoCommit=false, MERGE on the same KEY → holds write
   *       lock on the row (uncommitted).
   *   <li>Release the latch → worker calls real upsertPeriod → MERGE blocks waiting for our lock →
   *       LOCK_TIMEOUT (100ms) expires → H2 throws [90131].
   *   <li>Worker marks request FAILED. Test thread rolls back its lock connection.
   * </ol>
   */
  @Test
  public void upsertPeriod_failsWithMvccConflict_whenAnotherTransactionHoldsLock()
      throws Exception {
    YearMonth month = YearMonth.of(2024, 8);
    fakeClient.setGames(
        PLAYER,
        month,
        List.of(
            FakeChessClient.bulletPlayedGame("https://chess.com/game/conc-bullet-1"),
            FakeChessClient.bulletPlayedGame("https://chess.com/game/conc-bullet-2")));

    // Seed a row so the worker's MERGE does an UPDATE (required for MVCC conflict [90131]).
    DataSource ds = latchPeriodStore.getDataSource();
    try (Connection conn = ds.getConnection();
        PreparedStatement ps =
            conn.prepareStatement(
                """
                INSERT INTO indexed_periods
                    (player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """)) {
      ps.setString(1, PLAYER);
      ps.setString(2, PLATFORM);
      ps.setString(3, "2024-08");
      ps.setTimestamp(4, Timestamp.from(Instant.now()));
      ps.setBoolean(5, false);
      ps.setInt(6, 0);
      ps.setBoolean(7, false);
      ps.executeUpdate();
    }

    // Arm upsertPeriod latch.
    CountDownLatch upsertReached = new CountDownLatch(1);
    CountDownLatch proceedWithUpsert = new CountDownLatch(1);
    latchPeriodStore.armUpsert(upsertReached, proceedWithUpsert);

    // Submit the index request.
    post(
        "/v1/index",
        """
        {"player":"%s","platform":"%s","startMonth":"2024-08","endMonth":"2024-08","excludeBullet":false}
        """
            .formatted(PLAYER, PLATFORM));

    // Wait for worker to process games and reach upsertPeriod.
    upsertReached.await();

    // Hold a write lock on the SAME row the worker is about to MERGE.
    Connection lockConn = ds.getConnection();
    lockConn.setAutoCommit(false);
    try (PreparedStatement lockPs =
        lockConn.prepareStatement(
            """
            MERGE INTO indexed_periods
                (player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet)
            KEY (player, platform, year_month, exclude_bullet)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """)) {
      lockPs.setString(1, PLAYER);
      lockPs.setString(2, PLATFORM);
      lockPs.setString(3, "2024-08");
      lockPs.setTimestamp(4, Timestamp.from(Instant.now()));
      lockPs.setBoolean(5, true);
      lockPs.setInt(6, 0);
      lockPs.setBoolean(7, false); // same KEY the worker will use
      lockPs.executeUpdate();
    }

    // Release the worker — its MERGE blocks on our lock, times out (100ms), gets [90131].
    proceedWithUpsert.countDown();

    // Wait for the request to reach a terminal status.
    String status = awaitTerminalStatus(10_000);

    // Clean up the lock connection.
    lockConn.rollback();
    lockConn.close();

    assertThat(status)
        .describedAs(
            "index request should succeed under concurrent MERGE"
                + " (currently fails with H2 MVCC [90131])")
        .isEqualTo("COMPLETED");
  }

  // ===== HTTP helpers =====

  private String post(String path, String jsonBody) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + path))
            .header("Content-Type", "application/json")
            .POST(BodyPublishers.ofString(jsonBody))
            .build();
    HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isBetween(200, 299);
    return response.body();
  }

  private String get(String path) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder().uri(URI.create(baseUrl + path)).GET().build();
    HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
    return response.body();
  }

  /** Polls GET /v1/index until at least one request has the expected status. */
  private void awaitRequestStatus(String expectedStatus, long timeoutMs) throws Exception {
    long deadline = System.currentTimeMillis() + timeoutMs;
    ObjectMapper mapper = new ObjectMapper();
    while (System.currentTimeMillis() < deadline) {
      String body = get("/v1/index");
      var list = mapper.readTree(body);
      for (var node : list) {
        if (expectedStatus.equals(node.get("status").asText())) {
          return;
        }
      }
      Thread.sleep(100);
    }
    throw new AssertionError(
        "No request reached status " + expectedStatus + " within " + timeoutMs + "ms");
  }

  /** Polls until the most recent request reaches a terminal status (COMPLETED or FAILED). */
  private String awaitTerminalStatus(long timeoutMs) throws Exception {
    long deadline = System.currentTimeMillis() + timeoutMs;
    ObjectMapper mapper = new ObjectMapper();
    while (System.currentTimeMillis() < deadline) {
      String body = get("/v1/index");
      var list = mapper.readTree(body);
      // listRecent returns newest first — check the first (most recent) entry.
      if (list.size() > 0) {
        String status = list.get(0).get("status").asText();
        if ("COMPLETED".equals(status) || "FAILED".equals(status)) {
          return status;
        }
      }
      Thread.sleep(100);
    }
    throw new AssertionError("No request reached terminal status within " + timeoutMs + "ms");
  }
}
